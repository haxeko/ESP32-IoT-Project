/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Wi-Fi setup portal for chips without Bluetooth: an open access point, a DNS
// server that answers every name with the AP address (so phones show the
// "sign in to network" page automatically) and a one-page web form.
//
// The station credentials are kept in the esp_wifi NVS storage, which is where
// the Matter stack reads them on the next boot. A Matter factory reset clears
// them again (esp_wifi_restore()), which brings the setup page back.

#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <dhcpserver/dhcpserver.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "diag.h"
#include "wifi_setup.h"

static const char *TAG = "wifi_setup";

static constexpr int kMaxScanResults = 20;
static constexpr int kMaxConnectRetries = 3;
static constexpr uint64_t kConnectTimeoutUs = 20 * 1000 * 1000;
static constexpr uint64_t kRestartDelayUs = 5 * 1000 * 1000;
static constexpr size_t kMaxFormSize = 256;

enum class ConnectState { kIdle, kConnecting, kConnected, kFailed };

static std::atomic<ConnectState> s_state{ConnectState::kIdle};
static wifi_config_t s_sta_config;
static int s_retries;
static esp_timer_handle_t s_timeout_timer;
static esp_timer_handle_t s_restart_timer;
static esp_netif_t *s_ap_netif;

static wifi_ap_record_t s_scan[kMaxScanResults];
static uint16_t s_scan_count;

bool wifi_setup_is_provisioned(void)
{
    if (sizeof(CONFIG_DEFAULT_WIFI_SSID) > 1) {
        return true; // the Matter stack applies the built-in network itself
    }
    // The credentials live in the esp_wifi NVS storage: initialise the driver
    // just to read them, then release it for the Matter stack.
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return false;
    }
    wifi_config_t sta = {};
    const bool provisioned = esp_wifi_get_config(WIFI_IF_STA, &sta) == ESP_OK && sta.sta.ssid[0] != 0;
    if (provisioned) {
        ESP_LOGI(TAG, "Wi-Fi network: %s", (const char *)sta.sta.ssid);
    }
    esp_wifi_deinit();
    return provisioned;
}

// Lowest power the driver accepts (2 dBm), used after a brownout reset.
static constexpr int8_t kMinTxPower = 8;

void wifi_setup_limit_tx_power(void)
{
    // A brownout means the board's 3.3 V supply sagged, usually while the
    // radio was transmitting: retry with the lowest power.
    const int8_t power = diag_last_reset_was_brownout() ? kMinTxPower : CONFIG_WIFI_MAX_TX_POWER;
    esp_err_t err = esp_wifi_set_max_tx_power(power);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi TX power limited to %d.%02d dBm", power / 4, (power % 4) * 25);
    } else {
        ESP_LOGW(TAG, "Failed to set Wi-Fi TX power: %s", esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------
// Station: test the network chosen on the page

static void scan_networks()
{
    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        s_scan_count = 0;
        return;
    }
    uint16_t count = kMaxScanResults;
    if (esp_wifi_scan_get_ap_records(&count, s_scan) != ESP_OK) {
        count = 0;
    }
    // Strongest first, one entry per network name, hidden networks dropped.
    qsort(s_scan, count, sizeof(s_scan[0]), [](const void *a, const void *b) {
        return ((const wifi_ap_record_t *)b)->rssi - ((const wifi_ap_record_t *)a)->rssi;
    });
    uint16_t n = 0;
    for (uint16_t i = 0; i < count; i++) {
        bool keep = s_scan[i].ssid[0] != 0;
        for (uint16_t j = 0; keep && j < n; j++) {
            keep = strcmp((const char *)s_scan[i].ssid, (const char *)s_scan[j].ssid) != 0;
        }
        if (keep) {
            s_scan[n++] = s_scan[i];
        }
    }
    s_scan_count = n;
    ESP_LOGI(TAG, "Found %u networks", n);
}

static void start_connect(const char *ssid, const char *password)
{
    esp_timer_stop(s_timeout_timer);
    esp_wifi_disconnect();

    memset(&s_sta_config, 0, sizeof(s_sta_config));
    strlcpy((char *)s_sta_config.sta.ssid, ssid, sizeof(s_sta_config.sta.ssid));
    strlcpy((char *)s_sta_config.sta.password, password, sizeof(s_sta_config.sta.password));
    s_sta_config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    s_sta_config.sta.pmf_cfg.capable = true;
    s_sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    // Only keep the network in flash once it has been proven to work.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);

    ESP_LOGI(TAG, "Trying network \"%s\"", ssid);
    s_retries = 0;
    s_state = ConnectState::kConnecting;
    esp_timer_start_once(s_timeout_timer, kConnectTimeoutUs);
    esp_wifi_connect();
}

static void connect_failed()
{
    esp_timer_stop(s_timeout_timer);
    s_state = ConnectState::kFailed;
    esp_wifi_disconnect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "Phone " MACSTR " joined the setup network", MAC2STR(event->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "Phone " MACSTR " left the setup network, reason %u", MAC2STR(event->mac), event->reason);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_state != ConnectState::kConnecting) {
            return;
        }
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "Connection failed, reason %u", event->reason);
        if (++s_retries <= kMaxConnectRetries) {
            esp_wifi_connect();
        } else {
            connect_failed();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (s_state != ConnectState::kConnecting) {
            return;
        }
        esp_timer_stop(s_timeout_timer);
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected, got IP " IPSTR ", saving the network", IP2STR(&event->ip_info.ip));
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
        s_state = ConnectState::kConnected;
        // Leave time for the page to show the result, then start Matter.
        esp_timer_start_once(s_restart_timer, kRestartDelayUs);
    }
}

// ---------------------------------------------------------------------------
// Captive DNS: every A query is answered with the access point address

static void dns_task(void *arg)
{
    const uint32_t ap_ip = (uint32_t)(uintptr_t)arg; // network byte order

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "DNS server failed to start");
        if (sock >= 0) {
            close(sock);
        }
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[512];
    while (true) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &from_len);
        // Standard query (QR=0, OPCODE=0) with exactly one question.
        if (len < 12 || (buf[2] & 0xF8) != 0 || buf[4] != 0 || buf[5] != 1) {
            continue;
        }
        int pos = 12;
        while (pos < len && buf[pos] != 0) {
            pos += buf[pos] + 1;
        }
        pos += 5; // terminating zero, QTYPE, QCLASS
        if (pos > len) {
            continue;
        }
        const uint16_t qtype = (buf[pos - 4] << 8) | buf[pos - 3];

        buf[2] = 0x84 | (buf[2] & 0x01); // response, authoritative, keep RD
        buf[3] = 0x80;                   // recursion available, no error
        memset(&buf[6], 0, 6);           // no answers / authority / additional yet
        len = pos;                       // drop any additional records (EDNS)
        if (qtype == 1) {                // A
            const uint8_t answer[] = {
                0xC0, 0x0C,             // name: pointer to the question
                0x00, 0x01, 0x00, 0x01, // type A, class IN
                0x00, 0x00, 0x00, 0x3C, // TTL 60 s
                0x00, 0x04,             // address length
            };
            memcpy(&buf[len], answer, sizeof(answer));
            memcpy(&buf[len + sizeof(answer)], &ap_ip, 4);
            len += sizeof(answer) + 4;
            buf[7] = 1;
        }
        sendto(sock, buf, len, 0, (struct sockaddr *)&from, from_len);
    }
}

// ---------------------------------------------------------------------------
// Web page

static const char kPageHead[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>" CONFIG_DEVICE_DISPLAY_NAME "</title><style>"
    "body{font-family:-apple-system,Roboto,sans-serif;margin:0 auto;max-width:420px;padding:16px;color:#222}"
    "h1{font-size:22px;margin:8px 0}p{color:#555}"
    ".n{display:flex;justify-content:space-between;width:100%;padding:12px;margin:4px 0;border:1px solid #ddd;"
    "border-radius:8px;background:#fafafa;font-size:16px;text-align:left}"
    "input{width:100%;box-sizing:border-box;padding:12px;margin:6px 0 12px;font-size:16px;border:1px solid #bbb;"
    "border-radius:8px}"
    ".b{width:100%;padding:14px;border:0;border-radius:8px;background:#0a7cff;color:#fff;font-size:17px}"
    ".e{color:#c00}.ok{color:#080}a{color:#0a7cff}"
    "</style></head><body><h1>" CONFIG_DEVICE_DISPLAY_NAME "</h1>";

static const char kPageTail[] = "</body></html>";

static void send_html_escaped(httpd_req_t *req, const char *text)
{
    char out[128];
    size_t n = 0;
    for (const char *p = text; *p; p++) {
        if (n > sizeof(out) - 8) { // room for the longest entity and the terminator
            out[n] = 0;
            httpd_resp_sendstr_chunk(req, out);
            n = 0;
        }
        const char *rep = nullptr;
        switch (*p) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        default: out[n++] = *p; continue;
        }
        n += strlcpy(&out[n], rep, sizeof(out) - n);
    }
    out[n] = 0;
    httpd_resp_sendstr_chunk(req, out);
}

// Small grey diagnostics line at the bottom of every page.
static void send_page_tail(httpd_req_t *req)
{
    char report[512];
    diag_get_report(report, sizeof(report));
    httpd_resp_sendstr_chunk(req, "<p style=\"color:#999;font-size:12px;margin-top:32px\">");
    send_html_escaped(req, report);
    httpd_resp_sendstr_chunk(req, "</p>");
    httpd_resp_sendstr_chunk(req, kPageTail);
}

static esp_err_t send_form(httpd_req_t *req, const char *error)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, kPageHead);
    if (error) {
        httpd_resp_sendstr_chunk(req, "<p class=\"e\">");
        httpd_resp_sendstr_chunk(req, error);
        httpd_resp_sendstr_chunk(req, "</p>");
    }
    httpd_resp_sendstr_chunk(req, "<p>Chọn mạng Wi-Fi 2.4 GHz cho thiết bị:</p>");
    for (uint16_t i = 0; i < s_scan_count; i++) {
        const char *ssid = (const char *)s_scan[i].ssid;
        const int rssi = s_scan[i].rssi;
        char extra[96];
        snprintf(extra, sizeof(extra), "</span><span>%s %s</span></button>",
                 s_scan[i].authmode == WIFI_AUTH_OPEN ? "" : "&#128274;",
                 rssi > -60 ? "&#9679;&#9679;&#9679;" : rssi > -75 ? "&#9679;&#9679;&#9675;" : "&#9679;&#9675;&#9675;");
        httpd_resp_sendstr_chunk(req, "<button type=\"button\" class=\"n\" onclick=\"pick(this)\" data-s=\"");
        send_html_escaped(req, ssid);
        httpd_resp_sendstr_chunk(req, "\"><span>");
        send_html_escaped(req, ssid);
        httpd_resp_sendstr_chunk(req, extra);
    }
    httpd_resp_sendstr_chunk(req,
        "<p><a href=\"/?scan=1\">Quét lại</a></p>"
        "<form method=\"post\" action=\"/connect\">"
        "<label>Tên mạng (SSID)</label><input id=\"s\" name=\"ssid\" maxlength=\"32\" required>"
        "<label>Mật khẩu</label><input id=\"p\" name=\"pass\" type=\"password\" maxlength=\"64\">"
        "<button class=\"b\" type=\"submit\">Kết nối</button></form>"
        "<script>function pick(b){document.getElementById('s').value=b.dataset.s;"
        "document.getElementById('p').focus()}</script>");
    send_page_tail(req);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t send_status(httpd_req_t *req)
{
    const ConnectState state = s_state;
    if (state == ConnectState::kFailed || state == ConnectState::kIdle) {
        return send_form(req, state == ConnectState::kFailed
                                  ? "Không kết nối được. Kiểm tra lại tên mạng, mật khẩu, và mạng phải là 2.4 GHz."
                                  : nullptr);
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, kPageHead);
    if (state == ConnectState::kConnecting) {
        httpd_resp_sendstr_chunk(req, "<meta http-equiv=\"refresh\" content=\"3;url=/status\">"
                                      "<p>Đang kết nối tới <b>");
        send_html_escaped(req, (const char *)s_sta_config.sta.ssid);
        httpd_resp_sendstr_chunk(req, "</b>...</p><p>Nếu điện thoại bị ngắt khỏi mạng cài đặt, hãy kết nối lại "
                                      "rồi mở lại trang này.</p>");
    } else {
        httpd_resp_sendstr_chunk(req, "<p class=\"ok\"><b>Đã kết nối Wi-Fi.</b></p>"
                                      "<p>Thiết bị đang khởi động lại. Hãy đưa điện thoại về mạng Wi-Fi nhà, rồi thêm "
                                      "thiết bị vào ứng dụng nhà thông minh bằng mã Matter.</p>");
    }
    send_page_tail(req);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char query[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && strstr(query, "scan=1")) {
        if (s_state != ConnectState::kConnecting) {
            scan_networks();
        }
    }
    return send_status(req);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    return send_status(req);
}

// Decodes application/x-www-form-urlencoded text in place.
static void url_decode(char *s)
{
    char *out = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *out++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = { p[1], p[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            *out++ = *p;
        }
    }
    *out = 0;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[kMaxFormSize + 1];
    if (req->content_len > kMaxFormSize) {
        return send_form(req, "Dữ liệu quá dài.");
    }
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = 0;

    // Values are at most 32 / 64 bytes once decoded; keep room for %XX escapes.
    char ssid[3 * 32 + 1] = {};
    char pass[3 * 64 + 1] = {};
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "pass", pass, sizeof(pass));
    url_decode(ssid);
    url_decode(pass);

    if (ssid[0] == 0 || strlen(ssid) > 32) {
        return send_form(req, "Tên mạng không hợp lệ.");
    }
    if (strlen(pass) > 64 || (pass[0] && strlen(pass) < 8)) {
        return send_form(req, "Mật khẩu Wi-Fi phải từ 8 đến 64 ký tự (để trống nếu mạng không có mật khẩu).");
    }
    start_connect(ssid, pass);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/status");
    return httpd_resp_send(req, NULL, 0);
}

// Phones probe fixed URLs (captive.apple.com/hotspot-detect.html,
// connectivitycheck.gstatic.com/generate_204...). Redirecting every unknown
// URL to the form makes them open it as a "sign in to network" page.
static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static void start_web_server()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
    const httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL };
    const httpd_uri_t connect = { .uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler,
                                  .user_ctx = NULL };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &connect);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_handler);
}

// Phones need a DNS server from DHCP to detect the captive portal (and some
// refuse a network without one). Hand out the AP itself, plus the portal URL
// (DHCP option 114) for phones that support it.
static void configure_dhcp_server(const esp_netif_ip_info_t &ip_info)
{
    static char s_portal_uri[32]; // the DHCP server keeps the pointer
    snprintf(s_portal_uri, sizeof(s_portal_uri), "http://" IPSTR "/", IP2STR(&ip_info.ip));

    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ip_info.ip.addr;
    uint8_t offer_dns = OFFER_DNS;

    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, s_portal_uri,
                           strlen(s_portal_uri));
    esp_err_t err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DHCP server failed to start: %s", esp_err_to_name(err));
    }
}

// ---------------------------------------------------------------------------

void wifi_setup_run_portal(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    const esp_timer_create_args_t timeout_args = {
        .callback = [](void *) {
            ESP_LOGW(TAG, "Connection timed out");
            connect_failed();
        },
        .name = "wifi_setup_timeout",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timeout_args, &s_timeout_timer));
    const esp_timer_create_args_t restart_args = {
        .callback = [](void *) { esp_restart(); },
        .name = "wifi_setup_restart",
    };
    ESP_ERROR_CHECK(esp_timer_create(&restart_args, &s_restart_timer));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = {};
    const int ssid_len = snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s%02X%02X", CONFIG_WIFI_SETUP_AP_PREFIX,
                                  mac[4], mac[5]);
    ap.ap.ssid_len = (uint8_t)ssid_len;
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    strlcpy((char *)ap.ap.password, CONFIG_WIFI_SETUP_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.authmode = ap.ap.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ap.ap.pmf_cfg.required = false;

    // Scan first as a plain station: scanning hops channels, and a phone
    // trying to join the access point meanwhile would time out.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_setup_limit_tx_power();
    if (!diag_last_reset_was_brownout()) {
        scan_networks(); // after a brownout, skip it: the SSID can still be typed in
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    // 20 MHz channel and no modem sleep: the most compatible settings for phones.
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);
    esp_wifi_set_ps(WIFI_PS_NONE);
    wifi_setup_limit_tx_power();

    esp_netif_ip_info_t ip_info = {};
    esp_netif_get_ip_info(s_ap_netif, &ip_info);
    configure_dhcp_server(ip_info);

    start_web_server();
    xTaskCreate(dns_task, "dns", 4096, (void *)(uintptr_t)ip_info.ip.addr, 5, NULL);

    ESP_LOGW(TAG, "Wi-Fi setup: join \"%s\" and open http://" IPSTR, (const char *)ap.ap.ssid, IP2STR(&ip_info.ip));

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}
