/**
 * @file drd_handler.cpp
 * @brief Double-reset detection backend implementation.
 *
 * This module provides the implementation of the DoubleResetDetector class.
 * It supports RTC slow-memory and NVS persistence backends.
 *
 * The NVS backend reduces false double-reset detection during firmware
 * flashing by tracking the application image using the embedded ELF SHA-256
 * and treating tooling-style reset reasons as non-user resets.
 */

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C"
{
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>
}

#include "drd_handler.hpp"

#include "sdkconfig.h"

/// @brief Log tag for this module.
static const char *TAG = "drd_handler";

namespace
{
    // "DOBI ESEE" → "DOUBLEST", an arbitrary magic value.
    constexpr uint32_t kDrdMagic = 0xD0B1E5E5u;

    // RTC slow memory survives soft resets and normal resets, but not power loss.
    RTC_DATA_ATTR uint32_t s_rtc_magic = 0;

    // NVS backend keys.

    constexpr const char *kKeyMagic = "magic";
    // Kept for compatibility with earlier versions.
    constexpr const char *kKeyBoot = "last_boot_us";
    // Legacy 32-bit identity, kept for migration only.
    constexpr const char *kKeyAppHash = "app_hash";
    // Preferred firmware identity.
    constexpr const char *kKeyAppSha256 = "app_sha256";
    constexpr const char *kKeyDirty = "fw_dirty";
    constexpr const char *kKeyFirstBoot = "first_boot";

    constexpr size_t kSha256Len = 32;

    bool is_tooling_reset(esp_reset_reason_t reason)
    {
#if defined(CONFIG_DRD_SUPPRESS_TOOLING_RESETS)
        return (reason == ESP_RST_SW) ||
               (reason == ESP_RST_USB) ||
               (reason == ESP_RST_JTAG);
#else
        (void)reason;
        return false;
#endif
    }

    void sha256_to_hex(const uint8_t *sha,
                       size_t len,
                       char *out,
                       size_t out_len)
    {
        if (out == nullptr || out_len == 0)
        {
            return;
        }

        if (sha == nullptr || len == 0)
        {
            out[0] = '\0';
            return;
        }

        static const char *hex = "0123456789abcdef";
        const size_t needed = (len * 2U) + 1U;

        if (out_len < needed)
        {
            out[0] = '\0';
            return;
        }

        for (size_t i = 0; i < len; ++i)
        {
            const uint8_t v = sha[i];
            out[(i * 2U) + 0U] = hex[(v >> 4) & 0x0F];
            out[(i * 2U) + 1U] = hex[v & 0x0F];
        }

        out[len * 2U] = '\0';
    }

    bool get_current_app_sha256(std::array<uint8_t, kSha256Len> &out)
    {
        const esp_app_desc_t *app = esp_app_get_description();
        if (app == nullptr)
        {
            return false;
        }

        std::memcpy(out.data(), app->app_elf_sha256, kSha256Len);
        return true;
    }

    esp_err_t safe_nvs_init()
    {
        const esp_err_t err = nvs_flash_init();

        if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
            err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_LOGW(TAG,
                     "nvs_flash_init reported no free pages or new version. "
                     "Skipping erase. err=%s",
                     esp_err_to_name(err));
        }

        return err;
    }

    const char *reset_reason_to_string(esp_reset_reason_t reason)
    {
        switch (reason)
        {
        case ESP_RST_POWERON:
            return "Power-on reset";
        case ESP_RST_EXT:
            return "External pin reset";
        case ESP_RST_SW:
            return "Software reset";
        case ESP_RST_PANIC:
            return "Panic reset";
        case ESP_RST_INT_WDT:
            return "Interrupt watchdog reset";
        case ESP_RST_TASK_WDT:
            return "Task watchdog reset";
        case ESP_RST_WDT:
            return "Other watchdog reset";
        case ESP_RST_DEEPSLEEP:
            return "Deep-sleep wakeup";
        case ESP_RST_BROWNOUT:
            return "Brownout reset";
        case ESP_RST_SDIO:
            return "SDIO reset";
        case ESP_RST_USB:
            return "USB reset";
        case ESP_RST_JTAG:
            return "JTAG reset";
        default:
            return "Unknown reset reason";
        }
    }

} // namespace

namespace drd_handler
{
    DoubleResetDetector::DoubleResetDetector(Backend backend,
                                             const char *nvs_namespace)
        : backend_(backend),
          nvs_namespace_(nvs_namespace ? nvs_namespace : "drd")
    {
    }

    DoubleResetDetector::~DoubleResetDetector()
    {
        cancel_arm();
        cancel_disarm();

        if (nvs_ready_)
        {
            nvs_handle_t h = static_cast<nvs_handle_t>(nvs_handle_);
            nvs_close(h);
        }

        nvs_ready_ = false;
        nvs_handle_ = 0;
    }

    esp_err_t DoubleResetDetector::configure()
    {
        if (configured_)
        {
            return ESP_OK;
        }

        esp_err_t err = ESP_OK;

        if (backend_ == Backend::NVS)
        {
            err = safe_nvs_init();
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "NVS init failed in configure(). err=%s",
                         esp_err_to_name(err));
            }

            nvs_handle_t h = 0;
            err = nvs_open(nvs_namespace_, NVS_READWRITE, &h);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_open('%s') failed. err=%s",
                         nvs_namespace_,
                         esp_err_to_name(err));
                nvs_ready_ = false;
                nvs_handle_ = 0;
                configured_ = true;
                return err;
            }

            nvs_ready_ = true;
            nvs_handle_ = static_cast<uint32_t>(h);

            ESP_LOGI(TAG,
                     "DRD using NVS backend. namespace='%s'",
                     nvs_namespace_);
        }
        else
        {
            ESP_LOGI(TAG, "DRD using RTC backend");
        }

        configured_ = true;
        return err;
    }

    bool DoubleResetDetector::check_and_clear()
    {
        return check_and_clear(CONFIG_DRD_WINDOW_SECONDS);
    }

    bool DoubleResetDetector::check_and_clear(uint32_t window_s)
    {
        if (evaluated_)
        {
            return cached_result_;
        }

        evaluated_ = true;
        cached_result_ = false;

        const esp_reset_reason_t reason = esp_reset_reason();
        const bool tooling_reset = is_tooling_reset(reason);

        ESP_LOGI(TAG,
                 "Reset reason: %d (%s)",
                 static_cast<int>(reason),
                 reset_reason_to_string(reason));

        if (tooling_reset)
        {
            ESP_LOGI(TAG,
                     "Reset reason indicates tooling activity. "
                     "DRD arming will be delayed");
        }

        if (!configured_)
        {
            const esp_err_t err = configure();
            if (err != ESP_OK && backend_ == Backend::NVS)
            {
                ESP_LOGW(TAG,
                         "DRD configure failed with NVS backend. "
                         "Falling back to RTC behavior");
                backend_ = Backend::RTC;
            }
        }

        bool double_reset = false;

        if (backend_ == Backend::RTC)
        {
            if (tooling_reset)
            {
                s_rtc_magic = 0;
                cached_result_ = false;
                return false;
            }

            if (s_rtc_magic == kDrdMagic)
            {
                ESP_LOGI(TAG, "Double reset detected using RTC backend");
                double_reset = true;
                s_rtc_magic = 0;
            }
            else
            {
                ESP_LOGI(TAG,
                         "Arming RTC double-reset window. window_s=%" PRIu32,
                         window_s);
                s_rtc_magic = kDrdMagic;
                schedule_disarm(window_s);
            }

            cached_result_ = double_reset;
            return double_reset;
        }

        if (!nvs_ready_)
        {
            ESP_LOGW(TAG,
                     "NVS backend selected but not ready. "
                     "Skipping DRD detection");
            cached_result_ = false;
            return false;
        }

        nvs_handle_t h = static_cast<nvs_handle_t>(nvs_handle_);

        std::array<uint8_t, kSha256Len> current_sha = {};
        if (!get_current_app_sha256(current_sha))
        {
            ESP_LOGW(TAG,
                     "Failed to read app ELF SHA-256. Treating firmware as "
                     "dirty for DRD");
            firmware_id_dirty_ = true;
        }

        std::array<uint8_t, kSha256Len> stored_sha = {};
        size_t stored_len = kSha256Len;
        bool stored_sha_valid = false;

        const esp_err_t err_sha =
            nvs_get_blob(h, kKeyAppSha256, stored_sha.data(), &stored_len);

        if (err_sha == ESP_OK && stored_len == kSha256Len)
        {
            stored_sha_valid = true;
        }
        else if (err_sha == ESP_ERR_NVS_NOT_FOUND)
        {
            stored_sha_valid = false;
        }
        else
        {
            ESP_LOGW(TAG,
                     "nvs_get_blob(app_sha256) failed. err=%s",
                     esp_err_to_name(err_sha));
            stored_sha_valid = false;
        }

        uint32_t legacy_hash = 0;
        const esp_err_t err_legacy = nvs_get_u32(h, kKeyAppHash, &legacy_hash);
        const bool legacy_present = (err_legacy == ESP_OK);

        uint8_t first_boot_raw = 0;
        bool first_boot_seen = false;
        const esp_err_t err_fb_read =
            nvs_get_u8(h, kKeyFirstBoot, &first_boot_raw);

        if (err_fb_read == ESP_OK)
        {
            first_boot_seen = (first_boot_raw != 0);
        }
        else if (err_fb_read != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG,
                     "nvs_get_u8(first_boot) failed. err=%s",
                     esp_err_to_name(err_fb_read));
        }

        bool firmware_changed = false;

        if (!stored_sha_valid)
        {
            firmware_changed = true;

            if (legacy_present)
            {
                ESP_LOGI(TAG,
                         "No stored app SHA-256 but legacy app_hash exists. "
                         "Migrating DRD identity");
            }
            else
            {
                ESP_LOGI(TAG,
                         "No stored firmware identity. Treating as first boot "
                         "for this image");
            }
        }
        else if (std::memcmp(stored_sha.data(),
                             current_sha.data(),
                             kSha256Len) != 0)
        {
            firmware_changed = true;
            ESP_LOGI(TAG, "Firmware identity changed for DRD");
        }
        else
        {
            ESP_LOGI(TAG, "Firmware identity unchanged for DRD");
        }

        if (stored_sha_valid)
        {
            char stored_hex[(kSha256Len * 2U) + 1U] = {};
            sha256_to_hex(stored_sha.data(),
                          kSha256Len,
                          stored_hex,
                          sizeof(stored_hex));
            ESP_LOGI(TAG, "DRD stored app SHA-256: %s", stored_hex);
        }
        else
        {
            ESP_LOGI(TAG, "DRD stored app SHA-256: <none>");
        }

        {
            char current_hex[(kSha256Len * 2U) + 1U] = {};
            sha256_to_hex(current_sha.data(),
                          kSha256Len,
                          current_hex,
                          sizeof(current_hex));
            ESP_LOGI(TAG, "DRD current app SHA-256: %s", current_hex);
        }

        if (firmware_changed)
        {
            esp_err_t err = nvs_erase_key(h, kKeyMagic);
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            {
                ESP_LOGW(TAG,
                         "nvs_erase_key(magic) after firmware change failed. "
                         "err=%s",
                         esp_err_to_name(err));
            }

            err = nvs_set_blob(h,
                               kKeyAppSha256,
                               current_sha.data(),
                               kSha256Len);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_set_blob(app_sha256) failed. err=%s",
                         esp_err_to_name(err));
            }

            if (legacy_present)
            {
                err = nvs_erase_key(h, kKeyAppHash);
                if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
                {
                    ESP_LOGW(TAG,
                             "nvs_erase_key(app_hash) during migration failed. "
                             "err=%s",
                             esp_err_to_name(err));
                }
            }

            const esp_err_t err_dirty = nvs_set_u8(h, kKeyDirty, 1);
            if (err_dirty != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_set_u8(fw_dirty=1) failed. err=%s",
                         esp_err_to_name(err_dirty));
            }

            const esp_err_t err_fb = nvs_set_u8(h, kKeyFirstBoot, 1);
            if (err_fb != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_set_u8(first_boot=1) failed. err=%s",
                         esp_err_to_name(err_fb));
            }
            else
            {
                first_boot_seen = true;
            }

            err = nvs_commit(h);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_commit() after identity update failed. err=%s",
                         esp_err_to_name(err));
            }

            ESP_LOGI(TAG,
                     "DRD state reset due to firmware identity change. "
                     "Firmware marked dirty for DRD");
            firmware_id_dirty_ = true;
        }

        uint8_t fw_dirty_val = 0;
        bool firmware_dirty = false;
        const esp_err_t err_fd = nvs_get_u8(h, kKeyDirty, &fw_dirty_val);

        if (err_fd == ESP_OK)
        {
            firmware_dirty = (fw_dirty_val != 0);
        }
        else if (err_fd == ESP_ERR_NVS_NOT_FOUND)
        {
            firmware_dirty = firmware_changed || firmware_id_dirty_;
        }
        else
        {
            firmware_dirty = true;
            ESP_LOGW(TAG,
                     "nvs_get_u8(fw_dirty) failed. Assuming dirty. err=%s",
                     esp_err_to_name(err_fd));
        }

        ESP_LOGI(TAG,
                 "DRD status. firmware_dirty=%s, first_boot_seen=%s",
                 firmware_dirty ? "true" : "false",
                 first_boot_seen ? "true" : "false");

        uint32_t stored_magic = 0;
        const esp_err_t err_magic = nvs_get_u32(h, kKeyMagic, &stored_magic);

        if (err_magic != ESP_OK && err_magic != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG,
                     "nvs_get_u32(magic) failed. err=%s",
                     esp_err_to_name(err_magic));
        }

        if (!tooling_reset && !firmware_dirty && stored_magic == kDrdMagic)
        {
            ESP_LOGI(TAG, "Double reset detected using NVS backend");
            double_reset = true;

            cancel_disarm();

            esp_err_t err = nvs_erase_key(h, kKeyMagic);
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            {
                ESP_LOGW(TAG,
                         "nvs_erase_key(magic) failed. err=%s",
                         esp_err_to_name(err));
            }

            err = nvs_commit(h);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_commit() after erase failed. err=%s",
                         esp_err_to_name(err));
            }

            cached_result_ = double_reset;
            return double_reset;
        }

        if (firmware_dirty)
        {
            ESP_LOGI(TAG,
                     "Firmware dirty for DRD. Arming after delay. delay_s=%u, "
                     "window_s=%" PRIu32,
                     CONFIG_DRD_ARM_DELAY_SECONDS,
                     window_s);
            schedule_arm(window_s);
        }
        else if (tooling_reset)
        {
            ESP_LOGI(TAG,
                     "Tooling reset detected. Clearing DRD flag and arming "
                     "after delay. delay_s=%u, window_s=%" PRIu32,
                     CONFIG_DRD_ARM_DELAY_SECONDS,
                     window_s);

            esp_err_t err = nvs_erase_key(h, kKeyMagic);
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            {
                ESP_LOGW(TAG,
                         "nvs_erase_key(magic) during tooling reset failed. "
                         "err=%s",
                         esp_err_to_name(err));
            }

            err = nvs_commit(h);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_commit() after tooling reset clear failed. "
                         "err=%s",
                         esp_err_to_name(err));
            }

            schedule_arm(window_s);
        }
        else
        {
            ESP_LOGI(TAG,
                     "Firmware clean. Arming DRD window. window_s=%" PRIu32,
                     window_s);

            esp_err_t err = nvs_set_u32(h, kKeyMagic, kDrdMagic);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG,
                         "nvs_set_u32(magic) failed. err=%s",
                         esp_err_to_name(err));
            }
            else
            {
                err = nvs_commit(h);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG,
                             "nvs_commit() after arming failed. err=%s",
                             esp_err_to_name(err));
                }
                else
                {
                    schedule_disarm(window_s);
                }
            }
        }

        cached_result_ = double_reset;
        return double_reset;
    }

    void DoubleResetDetector::clear_flag()
    {
        if (backend_ == Backend::RTC)
        {
            s_rtc_magic = 0;
            ESP_LOGI(TAG, "RTC double-reset flag cleared");
            return;
        }

        if (!nvs_ready_)
        {
            ESP_LOGW(TAG, "clear_flag called but NVS is not ready");
            return;
        }

        nvs_handle_t h = static_cast<nvs_handle_t>(nvs_handle_);

        const char *keys[] = {
            kKeyMagic,
            kKeyBoot,
            kKeyDirty,
            kKeyFirstBoot,
            kKeyAppSha256,
            kKeyAppHash,
        };

        for (const char *key : keys)
        {
            const esp_err_t err = nvs_erase_key(h, key);
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            {
                ESP_LOGW(TAG,
                         "nvs_erase_key('%s') failed. err=%s",
                         key,
                         esp_err_to_name(err));
            }
        }

        const esp_err_t err_commit = nvs_commit(h);
        if (err_commit != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "nvs_commit() in clear_flag failed. err=%s",
                     esp_err_to_name(err_commit));
        }

        ESP_LOGI(TAG, "NVS double-reset flag cleared");
    }

    void DoubleResetDetector::cancel_arm()
    {
        if (arm_timer_ == nullptr)
        {
            return;
        }

        esp_err_t err = esp_timer_stop(arm_timer_);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG,
                     "esp_timer_stop(DRD arm) failed. err=%s",
                     esp_err_to_name(err));
        }

        err = esp_timer_delete(arm_timer_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "esp_timer_delete(DRD arm) failed. err=%s",
                     esp_err_to_name(err));
        }

        arm_timer_ = nullptr;
        arm_window_s_ = 0;
    }

    void DoubleResetDetector::schedule_arm(uint32_t window_s)
    {
        cancel_arm();

        arm_window_s_ = window_s;

        const uint32_t delay_s = CONFIG_DRD_ARM_DELAY_SECONDS;

        if (delay_s == 0)
        {
            arm_timer_cb(this);
            return;
        }

        const int64_t delay_us =
            static_cast<int64_t>(delay_s) * 1000000LL;

        esp_timer_create_args_t args = {};
        args.callback = &DoubleResetDetector::arm_timer_cb;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "drd_arm";

        esp_err_t err = esp_timer_create(&args, &arm_timer_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "esp_timer_create(DRD arm delay) failed. err=%s",
                     esp_err_to_name(err));
            arm_timer_ = nullptr;
            return;
        }

        err = esp_timer_start_once(arm_timer_, delay_us);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "esp_timer_start_once(DRD arm delay) failed. err=%s",
                     esp_err_to_name(err));
            esp_timer_delete(arm_timer_);
            arm_timer_ = nullptr;
        }
    }

    void DoubleResetDetector::arm_timer_cb(void *arg)
    {
        auto *self = static_cast<DoubleResetDetector *>(arg);
        if (self == nullptr)
        {
            return;
        }

        self->arm_timer_ = nullptr;

        if (self->backend_ != Backend::NVS || !self->nvs_ready_)
        {
            ESP_LOGW(TAG,
                     "DRD arm timer fired but NVS backend is not ready");
            return;
        }

        nvs_handle_t h = static_cast<nvs_handle_t>(self->nvs_handle_);

        ESP_LOGI(TAG,
                 "DRD arm delay elapsed. Marking firmware clean and arming. "
                 "window_s=%" PRIu32,
                 self->arm_window_s_);

        esp_err_t err = nvs_set_u8(h, kKeyDirty, 0);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "nvs_set_u8(fw_dirty=0) failed in arm callback. err=%s",
                     esp_err_to_name(err));
            return;
        }

        err = nvs_set_u32(h, kKeyMagic, kDrdMagic);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "nvs_set_u32(magic) failed in arm callback. err=%s",
                     esp_err_to_name(err));
            return;
        }

        err = nvs_commit(h);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "nvs_commit() failed in arm callback. err=%s",
                     esp_err_to_name(err));
            return;
        }

        self->schedule_disarm(self->arm_window_s_);
    }

    void DoubleResetDetector::cancel_disarm()
    {
        if (disarm_timer_ == nullptr)
        {
            return;
        }

        esp_err_t err = esp_timer_stop(disarm_timer_);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG,
                     "esp_timer_stop(DRD disarm) failed. err=%s",
                     esp_err_to_name(err));
        }

        err = esp_timer_delete(disarm_timer_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "esp_timer_delete(DRD disarm) failed. err=%s",
                     esp_err_to_name(err));
        }

        disarm_timer_ = nullptr;
    }

    void DoubleResetDetector::schedule_disarm(uint32_t window_s)
    {
        cancel_disarm();

        esp_timer_create_args_t args = {};
        args.callback = &DoubleResetDetector::disarm_timer_cb;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "drd_disarm";

        esp_err_t err = esp_timer_create(&args, &disarm_timer_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "esp_timer_create(DRD disarm) failed. err=%s",
                     esp_err_to_name(err));
            disarm_timer_ = nullptr;
            return;
        }

        constexpr uint64_t kUsPerSec = 1000000ULL;

        const uint64_t timeout_us =
            static_cast<uint64_t>(window_s) * kUsPerSec;

        err = esp_timer_start_once(disarm_timer_, timeout_us);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "esp_timer_start_once(DRD disarm) failed. err=%s",
                     esp_err_to_name(err));
            esp_timer_delete(disarm_timer_);
            disarm_timer_ = nullptr;
        }
    }

    void DoubleResetDetector::disarm_timer_cb(void *arg)
    {
        auto *self = static_cast<DoubleResetDetector *>(arg);
        if (self == nullptr)
        {
            return;
        }

        self->disarm_timer_ = nullptr;

        if (self->backend_ == Backend::RTC)
        {
            self->clear_flag();
            return;
        }

        if (!self->nvs_ready_)
        {
            ESP_LOGW(TAG,
                     "DRD disarm timer fired but NVS backend is not ready");
            return;
        }

        nvs_handle_t h = static_cast<nvs_handle_t>(self->nvs_handle_);

        const esp_err_t err = nvs_erase_key(h, kKeyMagic);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG,
                     "nvs_erase_key(magic) failed in disarm callback. err=%s",
                     esp_err_to_name(err));
        }

        const esp_err_t err_commit = nvs_commit(h);
        if (err_commit != ESP_OK)
        {
            ESP_LOGW(TAG,
                     "nvs_commit() failed in disarm callback. err=%s",
                     esp_err_to_name(err_commit));
        }

        ESP_LOGI(TAG, "DRD disarm window elapsed. DRD magic cleared");
    }

#if defined(CONFIG_DRD_BACKEND_NVS)
    static DoubleResetDetector g_detector(Backend::NVS,
                                          CONFIG_DRD_NVS_NAMESPACE);
#else
    static DoubleResetDetector g_detector(Backend::RTC,
                                          CONFIG_DRD_NVS_NAMESPACE);
#endif

    DoubleResetDetector &get()
    {
        return g_detector;
    }

} // namespace drd_handler
