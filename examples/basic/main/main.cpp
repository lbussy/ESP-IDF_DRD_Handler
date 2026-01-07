#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>

#include <esp_err.h>
#include <esp_log.h>
#include <sdkconfig.h>

#include "drd_handler.hpp"

namespace
{
    constexpr const char *kTag = "drd_basic";

    void configure_led(gpio_num_t gpio)
    {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << static_cast<uint32_t>(gpio));
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;

        const esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(kTag, "gpio_config failed: %s", esp_err_to_name(err));
            return;
        }

        (void)gpio_set_level(gpio, 0);
    }

    void blink_task(void *arg)
    {
        const uint32_t delay_ms = *static_cast<uint32_t *>(arg);

        const gpio_num_t led_gpio = static_cast<gpio_num_t>(CONFIG_EXAMPLE_STATUS_LED_GPIO);

        configure_led(led_gpio);

        while (true)
        {
            (void)gpio_set_level(led_gpio, 1);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            (void)gpio_set_level(led_gpio, 0);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "Starting drd_handler basic example");

    // Configure the detector (initializes NVS if that backend is selected).
    const esp_err_t cfg_err = drd_handler::get().configure();
    if (cfg_err != ESP_OK)
    {
        ESP_LOGE(kTag, "drd_handler configure failed: %s", esp_err_to_name(cfg_err));
        // Continue boot; DRD is optional behavior.
    }

    // Evaluate once per boot; the result is cached inside the component.
    const bool drd = drd_handler::get().check_and_clear();

    if (drd)
        ESP_LOGW(kTag, "Double reset detected: entering alternate mode");
    else
        ESP_LOGI(kTag, "No double reset detected: normal mode");

    const uint32_t blink_ms =
        drd ? static_cast<uint32_t>(CONFIG_EXAMPLE_BLINK_DRD_MS)
            : static_cast<uint32_t>(CONFIG_EXAMPLE_BLINK_NORMAL_MS);

    // Note: Pass blink_ms by value via a static so the task has a stable pointer.
    static uint32_t s_blink_ms = 0;
    s_blink_ms = blink_ms;

    BaseType_t ok =
        xTaskCreate(blink_task, "blink", 2048, &s_blink_ms, tskIDLE_PRIORITY + 1, nullptr);

    if (ok != pdPASS)
        ESP_LOGE(kTag, "Failed to create blink task");
}
