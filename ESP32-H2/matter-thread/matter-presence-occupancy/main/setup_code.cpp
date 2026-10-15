// See setup_code.h. ESP-IDF v6.0.2, esp-matter 1.6.1 (connectedhomeip 3d1c9e3c).

#include <sdkconfig.h>

#include "setup_code.h"

#include <string.h>

#include <esp_log.h>

#include <lib/support/Span.h>
#include <platform/CHIPDeviceConfig.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#if CONFIG_UNIQUE_SETUP_CODE
#include <esp_mac.h>
#include <esp_matter_providers.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <crypto/CHIPCryptoPAL.h>
#include <platform/CommissionableDataProvider.h>
#endif

// Unused when both CONFIG_UNIQUE_SETUP_CODE and CONFIG_PRINT_ONBOARDING_CODES are off (factory build)
[[maybe_unused]] static const char *TAG = "setup_code";

#if CONFIG_UNIQUE_SETUP_CODE
namespace {

using chip::ByteSpan;
using chip::MutableByteSpan;

constexpr char kPartition[] = CONFIG_UNIQUE_SETUP_CODE_PARTITION;
constexpr char kNamespace[] = "setup-code";
// Lowest PBKDF2 cost allowed by the spec, the same as the SDK test parameters
constexpr uint32_t kIterations = chip::Crypto::kSpake2p_Min_PBKDF_Iterations;
constexpr size_t kSaltLen = chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length;
constexpr size_t kVerifierLen = chip::Crypto::kSpake2p_VerifierSerialized_Length;
// SDK test discriminator: never generated, so a board is never mistaken for a test-code board
constexpr uint16_t kTestDiscriminator = 0xF00;
constexpr uint16_t kDiscriminatorMask = 0xFFF;

// Random bytes: CHIP DRBG output hashed together with the factory MAC and a counter. The DRBG gives the
// unpredictability; the MAC makes the input differ on every chip even if two chips' entropy were alike.
class RandomStream
{
public:
    CHIP_ERROR Init()
    {
        uint8_t mac[6] = {};
        ReturnErrorOnFailure(chip::Crypto::DRBG_get_bytes(mSeed, sizeof(mSeed) - sizeof(mac)));
        if (esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY) != ESP_OK) {
            return CHIP_ERROR_INTERNAL;
        }
        memcpy(&mSeed[sizeof(mSeed) - sizeof(mac)], mac, sizeof(mac));
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR Next(uint8_t (&out)[chip::Crypto::kSHA256_Hash_Length])
    {
        uint8_t block[sizeof(mSeed) + sizeof(mCounter)];
        memcpy(block, mSeed, sizeof(mSeed));
        memcpy(&block[sizeof(mSeed)], &mCounter, sizeof(mCounter));
        mCounter++;
        return chip::Crypto::Hash_SHA256(block, sizeof(block), out);
    }

    CHIP_ERROR NextU32(uint32_t &value)
    {
        uint8_t h[chip::Crypto::kSHA256_Hash_Length];
        ReturnErrorOnFailure(Next(h));
        memcpy(&value, h, sizeof(value));
        return CHIP_NO_ERROR;
    }

private:
    uint8_t mSeed[32 + 6] = {};
    uint32_t mCounter = 0;
};

class UniqueSetupCodeProvider : public chip::DeviceLayer::CommissionableDataProvider
{
public:
    void Init() { mLock = xSemaphoreCreateMutexStatic(&mLockBuffer); }

    CHIP_ERROR GetSetupDiscriminator(uint16_t &setupDiscriminator) override
    {
        ReturnErrorOnFailure(Load());
        setupDiscriminator = mDiscriminator;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR SetSetupDiscriminator(uint16_t setupDiscriminator) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

    CHIP_ERROR GetSpake2pIterationCount(uint32_t &iterationCount) override
    {
        ReturnErrorOnFailure(Load());
        iterationCount = mIterations;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetSpake2pSalt(MutableByteSpan &saltBuf) override
    {
        ReturnErrorOnFailure(Load());
        return chip::CopySpanToMutableSpan(ByteSpan(mSalt, mSaltLen), saltBuf);
    }

    CHIP_ERROR GetSpake2pVerifier(MutableByteSpan &verifierBuf, size_t &verifierLen) override
    {
        ReturnErrorOnFailure(Load());
        verifierLen = kVerifierLen;
        return chip::CopySpanToMutableSpan(ByteSpan(mVerifier), verifierBuf);
    }

    // Development use only: lets the board print its own QR code (setup_code_print())
    CHIP_ERROR GetSetupPasscode(uint32_t &setupPasscode) override
    {
        ReturnErrorOnFailure(Load());
        setupPasscode = mPasscode;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR SetSetupPasscode(uint32_t setupPasscode) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
    SemaphoreHandle_t mLock = nullptr;
    StaticSemaphore_t mLockBuffer;
    bool mLoaded = false;
    uint32_t mPasscode = 0;
    uint16_t mDiscriminator = 0;
    uint32_t mIterations = 0;
    uint8_t mSalt[kSaltLen] = {};
    size_t mSaltLen = 0;
    uint8_t mVerifier[kVerifierLen] = {};

    // Lazily on first use (by then the radio is up, see the README), then cached
    CHIP_ERROR Load()
    {
        if (mLoaded) {
            return CHIP_NO_ERROR;
        }
        VerifyOrReturnError(mLock != nullptr, CHIP_ERROR_INCORRECT_STATE);
        xSemaphoreTake(mLock, portMAX_DELAY);
        CHIP_ERROR err = CHIP_NO_ERROR;
        if (!mLoaded) {
            if (ReadStored()) {
                ESP_LOGI(TAG, "Per-board setup code loaded from %s/%s, discriminator %u", kPartition, kNamespace,
                         mDiscriminator);
                mLoaded = true;
            } else {
                err = Generate();
                if (err == CHIP_NO_ERROR) {
                    mLoaded = true;
                    if (Store()) {
                        ESP_LOGW(TAG, "New per-board setup code generated and stored in %s/%s, discriminator %u",
                                 kPartition, kNamespace, mDiscriminator);
                    } else {
                        ESP_LOGE(TAG, "New per-board setup code NOT stored: it changes at the next boot");
                    }
                } else {
                    ESP_LOGE(TAG, "Setup code generation failed: %" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        xSemaphoreGive(mLock);
        return err;
    }

    bool ReadStored()
    {
        nvs_handle_t h;
        if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &h) != ESP_OK) {
            return false;
        }
        size_t salt_len = sizeof(mSalt);
        size_t verifier_len = sizeof(mVerifier);
        const bool ok = nvs_get_u32(h, "passcode", &mPasscode) == ESP_OK &&
                        nvs_get_u16(h, "discriminator", &mDiscriminator) == ESP_OK &&
                        nvs_get_u32(h, "iterations", &mIterations) == ESP_OK &&
                        nvs_get_blob(h, "salt", mSalt, &salt_len) == ESP_OK &&
                        nvs_get_blob(h, "verifier", mVerifier, &verifier_len) == ESP_OK;
        nvs_close(h);
        mSaltLen = salt_len;
        return ok && chip::PayloadContents::IsValidSetupPIN(mPasscode) && mDiscriminator <= kDiscriminatorMask &&
               mIterations >= chip::Crypto::kSpake2p_Min_PBKDF_Iterations &&
               mIterations <= chip::Crypto::kSpake2p_Max_PBKDF_Iterations &&
               salt_len >= chip::Crypto::kSpake2p_Min_PBKDF_Salt_Length && salt_len <= kSaltLen &&
               verifier_len == kVerifierLen;
    }

    CHIP_ERROR Generate()
    {
        RandomStream rnd;
        ReturnErrorOnFailure(rnd.Init());

        // Passcode: uniform in 1..99999998 (rejection sampling on 27 bits), not one of the forbidden values
        // (00000000, 11111111 ... 99999999, 12345678, 87654321)
        uint32_t value = 0;
        do {
            ReturnErrorOnFailure(rnd.NextU32(value));
            value &= (1u << chip::kSetupPINCodeFieldLengthInBits) - 1;
        } while (!chip::PayloadContents::IsValidSetupPIN(value));
        mPasscode = value;

        do {
            ReturnErrorOnFailure(rnd.NextU32(value));
            value &= kDiscriminatorMask;
        } while (value == kTestDiscriminator);
        mDiscriminator = static_cast<uint16_t>(value);

        uint8_t h[chip::Crypto::kSHA256_Hash_Length];
        static_assert(sizeof(h) >= kSaltLen, "one hash block fills the salt");
        ReturnErrorOnFailure(rnd.Next(h));
        memcpy(mSalt, h, kSaltLen);
        mSaltLen = kSaltLen;
        mIterations = kIterations;

        chip::Crypto::Spake2pVerifier verifier;
        ReturnErrorOnFailure(verifier.Generate(mIterations, ByteSpan(mSalt, mSaltLen), mPasscode));
        MutableByteSpan out(mVerifier);
        ReturnErrorOnFailure(verifier.Serialize(out));
        VerifyOrReturnError(out.size() == kVerifierLen, CHIP_ERROR_INTERNAL);
        return CHIP_NO_ERROR;
    }

    bool Store()
    {
        nvs_handle_t h;
        if (nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &h) != ESP_OK) {
            return false;
        }
        const bool ok = nvs_set_u32(h, "passcode", mPasscode) == ESP_OK &&
                        nvs_set_u16(h, "discriminator", mDiscriminator) == ESP_OK &&
                        nvs_set_u32(h, "iterations", mIterations) == ESP_OK &&
                        nvs_set_blob(h, "salt", mSalt, mSaltLen) == ESP_OK &&
                        nvs_set_blob(h, "verifier", mVerifier, sizeof(mVerifier)) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
        return ok;
    }
};

UniqueSetupCodeProvider s_provider;

} // namespace
#endif // CONFIG_UNIQUE_SETUP_CODE

esp_err_t setup_code_init(void)
{
#if CONFIG_UNIQUE_SETUP_CODE
    // The partition holds nothing else in these builds: reformat it if it does not contain NVS data
    esp_err_t err = nvs_flash_init_partition(kPartition);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Partition %s is not NVS (%s): erasing it", kPartition, esp_err_to_name(err));
        err = nvs_flash_erase_partition(kPartition);
        if (err == ESP_OK) {
            err = nvs_flash_init_partition(kPartition);
        }
    }
    if (err != ESP_OK) {
        // The provider still works, but a new code would be generated at every boot
        ESP_LOGE(TAG, "Partition %s unusable (%s)", kPartition, esp_err_to_name(err));
    }
    s_provider.Init();
    esp_matter::set_custom_commissionable_data_provider(&s_provider);
    return err;
#else
    return ESP_OK;
#endif
}

void setup_code_print(bool on_network)
{
#if CONFIG_PRINT_ONBOARDING_CODES
    const chip::RendezvousInformationFlags flags(on_network ? chip::RendezvousInformationFlag::kOnNetwork
                                                            : chip::RendezvousInformationFlag::kBLE);
    char qr[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
    chip::MutableCharSpan qr_span(qr);
    if (GetQRCode(qr_span, flags) != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Cannot build the QR code");
        return;
    }
    // Manual pairing code: 11 digits, shorter than the QR code
    char manual[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
    chip::MutableCharSpan manual_span(manual);
    const bool manual_ok = GetManualPairingCode(manual_span, flags) == CHIP_NO_ERROR;

    char url[160];
    const bool url_ok = GetQRCodeUrl(url, sizeof(url), chip::CharSpan(qr_span.data(), qr_span.size())) == CHIP_NO_ERROR;

    ESP_LOGI(TAG, "Setup code of this board (%s%s):", on_network ? "Thread / DNS-SD" : "BLE",
             CHIP_DEVICE_CONFIG_ENABLE_TEST_SETUP_PARAMS ? ", SDK TEST CODE shared by every board" : "");
    ESP_LOGI(TAG, "  QR code: %s", qr_span.data());
    if (url_ok) {
        ESP_LOGI(TAG, "  QR image: %s", url);
    }
    if (manual_ok) {
        ESP_LOGI(TAG, "  Manual pairing code: %s", manual_span.data());
    }
#else
    (void)on_network;
#endif
}
