// Host tests of the setup page form handling (main/portal_form.c)
#include <string.h>

#include "check.h"
#include "portal_form.h"

static void check_decode(const char *in, const char *expected, int ok)
{
    char buf[256];
    strcpy(buf, in);
    const bool result = form_url_decode(buf);
    CHECK_EQ(result, ok);
    if (ok) {
        CHECK(strcmp(buf, expected) == 0);
        if (strcmp(buf, expected) != 0) {
            printf("    decode(\"%s\") = \"%s\", expected \"%s\"\n", in, buf, expected);
        }
    }
}

static void test_url_decode(void)
{
    check_decode("", "", 1);
    check_decode("plain", "plain", 1);
    check_decode("My+Home+WiFi", "My Home WiFi", 1);
    check_decode("a%20b%2Bc", "a b+c", 1);
    check_decode("%41%62%7e", "Ab~", 1);
    check_decode("Nh%C3%A0+t%C3%B4i", "Nh\xC3\xA0 t\xC3\xB4i", 1);     // "Nhà tôi" in UTF-8
    check_decode("100%", "100%", 1);                                  // lone % at the end is kept
    check_decode("50%2", "50%2", 1);                                  // truncated escape is kept
    check_decode("%zz%g1", "%zz%g1", 1);                              // invalid escapes are kept
    check_decode("%%41", "%A", 1);
    check_decode("pass%00word", "", 0);                               // NUL cannot be stored
}

static void test_creds(void)
{
    char ssid33[34];
    memset(ssid33, 'x', 33);
    ssid33[33] = 0;
    char hex64[65];
    memset(hex64, 'a', 64);
    hex64[64] = 0;
    char text64[65];
    memcpy(text64, hex64, sizeof(text64));
    text64[10] = 'z';
    char pass63[64];
    memset(pass63, 'p', 63);
    pass63[63] = 0;

    CHECK_EQ(wifi_creds_check("Home", ""), WIFI_CREDS_OK);             // open network
    CHECK_EQ(wifi_creds_check("Home", "12345678"), WIFI_CREDS_OK);
    CHECK_EQ(wifi_creds_check("Home", pass63), WIFI_CREDS_OK);
    CHECK_EQ(wifi_creds_check("Home", hex64), WIFI_CREDS_OK);           // raw PSK
    CHECK_EQ(wifi_creds_check("", "12345678"), WIFI_CREDS_BAD_SSID);
    CHECK_EQ(wifi_creds_check(ssid33, "12345678"), WIFI_CREDS_BAD_SSID);
    CHECK_EQ(wifi_creds_check(ssid33 + 1, "12345678"), WIFI_CREDS_OK);  // 32 bytes is the maximum
    CHECK_EQ(wifi_creds_check("Home", "1234567"), WIFI_CREDS_BAD_PASSWORD);
    CHECK_EQ(wifi_creds_check("Home", text64), WIFI_CREDS_BAD_PASSWORD);
    CHECK_EQ(wifi_creds_check("Home", "x"), WIFI_CREDS_BAD_PASSWORD);
}

int main(void)
{
    test_url_decode();
    test_creds();
    return test_result("portal_form");
}
