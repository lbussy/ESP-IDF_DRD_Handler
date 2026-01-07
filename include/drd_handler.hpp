/**
 * @file drd_handler.hpp
 * @brief Double-reset detection utilities.
 *
 * This component detects a user double-reset event within a configurable
 * time window. It supports two persistence backends:
 * - RTC slow memory, for soft-reset style boards.
 * - NVS, for boards where the reset button behaves like power cycling.
 *
 * The NVS backend also suppresses false double-reset detection during
 * firmware flashing by:
 * - Tracking the current application image via the embedded ELF SHA-256.
 * - Treating tooling-style reset reasons as non-user resets.
 *
 * @note
 * Although the DoubleResetDetector type is instantiable, this component
 * is designed to operate as a singleton. A single global instance is
 * provided via drd_handler::get(), and that instance owns all persistent
 * DRD state. Creating additional instances is unsupported and may result
 * in undefined behavior.
 */

#pragma once

#include <cstdint>

#include <esp_err.h>
#include <esp_timer.h>

/// Double-reset detection utilities.
namespace drd_handler
{

    /**
     * @brief Backend storage used for double reset detection.
     *
     * RTC uses RTC slow memory. NVS uses non-volatile storage to persist
     * state across more reset types.
     */
    enum class Backend : uint8_t
    {
        RTC, ///< Use RTC slow memory.
        NVS  ///< Use NVS namespace.
    };

    /**
     * @brief Detects double reset events within a configurable time window.
     *
     * The detector tracks state across resets using either RTC slow memory
     * or NVS, depending on the selected backend. The first call in a boot
     * evaluates the double reset condition and caches the result so later
     * calls are inexpensive.
     */
    class DoubleResetDetector
    {
    public:
        /**
         * @brief Construct a detector with the given backend.
         *
         * @param backend       Storage backend to use.
         * @param nvs_namespace NVS namespace for state when backend is NVS.
         */
        explicit DoubleResetDetector(Backend backend,
                                     const char *nvs_namespace = "drd");

        /// Destructor cleans up any active timers.
        ~DoubleResetDetector();

        DoubleResetDetector(const DoubleResetDetector &) = delete;
        DoubleResetDetector &operator=(const DoubleResetDetector &) = delete;

        DoubleResetDetector(DoubleResetDetector &&) = delete;
        DoubleResetDetector &operator=(DoubleResetDetector &&) = delete;

        /**
         * @brief Configure the detector backend.
         *
         * For the NVS backend this initializes NVS (without erasing data)
         * and opens the configured namespace. For the RTC backend this
         * performs no special work.
         *
         * @return ESP_OK on success or an ESP-IDF error code.
         */
        esp_err_t configure();

        /**
         * @brief Check and clear using the configured window.
         *
         * Uses CONFIG_DRD_WINDOW_SECONDS from Kconfig as the detection
         * window. The result is cached for the remainder of the boot.
         *
         * @return true if a double reset was detected.
         * @return false otherwise.
         */
        [[nodiscard]] bool check_and_clear();

        /**
         * @brief Check and clear using an explicit window.
         *
         * The first call in a boot evaluates the double reset condition,
         * updates internal state and caches the result. Later calls in
         * the same boot return the cached value.
         *
         * @param window_s Detection window in seconds.
         *
         * @return true if a double reset was detected.
         * @return false otherwise.
         */
        [[nodiscard]] bool check_and_clear(uint32_t window_s);

        /**
         * @brief Clear any stored double reset state.
         *
         * For the RTC backend this clears the RTC markers. For the NVS
         * backend this removes the stored keys from the namespace.
         */
        void clear_flag();

    private:
        Backend backend_;
        /// Borrowed pointer, usually a static string literal.
        const char *nvs_namespace_ = "drd";
        bool configured_ = false;

        bool nvs_ready_ = false;
        /// NVS handle stored as an integer; valid only when nvs_ready_ is true.
        uint32_t nvs_handle_ = 0;

        /// Indicates whether this boot has already been evaluated.
        bool evaluated_ = false;
        /// Cached result for the current boot.
        bool cached_result_ = false;

        /// Timer that disarms the active double reset window.
        esp_timer_handle_t disarm_timer_ = nullptr;
        /// Timer that delays arming after a firmware update.
        esp_timer_handle_t arm_timer_ = nullptr;
        /// Window length to use when arming after the delay.
        uint32_t arm_window_s_ = 0;
        /// Tracks whether the firmware identity is still considered dirty.
        bool firmware_id_dirty_ = false;

        void schedule_disarm(uint32_t window_s);
        void cancel_disarm();
        static void disarm_timer_cb(void *arg);

        void cancel_arm();
        void schedule_arm(uint32_t window_s);
        static void arm_timer_cb(void *arg);
    };

    /**
     * @brief Get the global DoubleResetDetector instance.
     *
     * The instance is configured at link time based on Kconfig options.
     *
     * @return Reference to the global detector.
     */
    DoubleResetDetector &get();

    /**
     * @brief Convenience wrapper around the global detector.
     *
     * @param window_s Detection window in seconds.
     *
     * @return true if a double reset was detected.
     * @return false otherwise.
     */
    [[nodiscard]] inline bool check_and_clear(uint32_t window_s)
    {
        return get().check_and_clear(window_s);
    }

    /**
     * @brief Convenience wrapper that clears the global DRD state.
     */
    inline void clear_flag()
    {
        get().clear_flag();
    }

} // namespace drd_handler
