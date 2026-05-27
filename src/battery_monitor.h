#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h" // 包含校准句柄
#include "driver/gpio.h"

/**
 * @brief ESP32-C3 电池电压监测类
 *
 * 该类使用 ESP-IDF 的 ADC One-Shot 模式来读取电池电压。
 * 它支持电压分压器，并集成了 ADC 校准功能以提高测量精度。
 */
class BatteryMonitor
{
public:
    /**
     * @brief 构造函数。
     * @param adc_unit 要使用的 ADC 单元 (例如, ADC_UNIT_1)。
     * @param adc_channel 要使用的 ADC 通道 (例如, ADC_CHANNEL_0)。
     * @param voltage_divider_ratio 电压分压器的比率 (例如, 对于 1/2 分压器，设置为 2.0)。
     *                              如果未使用分压器，请设置为 1.0。
     */
    BatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel, float voltage_divider_ratio = 1.0f);

    /**
     * @brief 析构函数。清理 ADC 资源。
     */
    ~BatteryMonitor();

    /**
     * @brief 初始化 ADC 用于电池监测。
     * @param attenuation ADC 衰减。
     *        对于 ESP32-C3，建议使用 ADC_ATTEN_DB_12 以获得 0-3.3V 范围。
     * @return 成功返回 ESP_OK，否则返回错误码。
     */
    esp_err_t init(adc_atten_t attenuation = ADC_ATTEN_DB_12);
    /**
     * @brief 读取当前电池电压。
     * @param voltage 指向浮点数的指针，用于存储测量的电压。
     * @return 成功返回 ESP_OK，否则返回错误码。
     */
    esp_err_t read_voltage(float *voltage);

private:
    adc_unit_t _adc_unit;
    adc_channel_t _adc_channel;
    float _voltage_divider_ratio;
    adc_oneshot_unit_handle_t _adc_handle;
    adc_atten_t _attenuation;
    adc_cali_handle_t _adc_cali_handle; // 每个实例的校准句柄
    bool _adc_calibration_is_init;

    // 私有辅助函数，用于初始化 ADC 校准
    bool _adc_calibration_init();
    // 私有辅助函数，用于去初始化 ADC 校准
    void _adc_calibration_deinit();
    // 私有辅助函数，用于将原始 ADC 值转换为毫伏 (校准失败或不支持时的备用方案)
    uint32_t _adc_raw_to_voltage_mv(int raw_adc);
};

#endif // BATTERY_MONITOR_H