#include "battery_monitor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

class PowerManager {
private:
    BatteryMonitor batteryMonitor;
    bool usb_power_detected;
    const int USB_DETECT_PIN = GPIO_NUM_4;  // USB检测引脚

public:
    PowerManager() : batteryMonitor(ADC_UNIT_1, ADC_CHANNEL_3, 2.0f), usb_power_detected(false) {}

    void init() {
        // 配置 USB 检测引脚
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << USB_DETECT_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,  // 默认下拉
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);

        // 初始化电池监测
        batteryMonitor.init(ADC_ATTEN_DB_12);
    }

    void update_usb_status() {
        // 实际检测 USB 是否连接
        usb_power_detected = (gpio_get_level(static_cast<gpio_num_t>(USB_DETECT_PIN)) == 1);
    }

    void print_power_status() {
        update_usb_status();  // 先更新状态

        float voltage;
        if (batteryMonitor.read_voltage(&voltage) == ESP_OK) {
            if (usb_power_detected) {
                ESP_LOGI("POWER", "USB Powered - Battery voltage: %.2fV", voltage);
            } else {
                ESP_LOGI("POWER", "Battery Powered - Voltage: %.2fV", voltage);
            }
        }
    }
};

// 使用示例
extern "C" void app_main() {
    PowerManager pm;
    pm.init();

    while (1) {
        pm.print_power_status();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}