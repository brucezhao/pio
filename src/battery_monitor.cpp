#include "battery_monitor.h"
#include "esp_log.h"
#include "esp_adc/adc_cali_scheme.h" // 包含校准方案

static const char *TAG = "BatteryMonitor";

BatteryMonitor::BatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel, float voltage_divider_ratio)
    : _adc_unit(adc_unit), _adc_channel(adc_channel), _voltage_divider_ratio(voltage_divider_ratio),
      _adc_handle(NULL), _attenuation(ADC_ATTEN_DB_12), _adc_cali_handle(NULL), _adc_calibration_is_init(false)
{
    // 构造函数仅设置成员变量，实际初始化在 init() 中进行
}

BatteryMonitor::~BatteryMonitor()
{
    if (_adc_handle)
    {
        esp_err_t ret = adc_oneshot_del_unit(_adc_handle);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to delete ADC unit: %s", esp_err_to_name(ret));
        }
        _adc_handle = NULL;
    }
    if (_adc_calibration_is_init)
    {
        _adc_calibration_deinit();
    }
}

bool BatteryMonitor::_adc_calibration_init()
{
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FMT_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = _adc_unit,
            .atten = _attenuation,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &_adc_cali_handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
        else if (ret == ESP_ERR_NOT_SUPPORTED || ret == ESP_ERR_INVALID_ARG)
        {
            ESP_LOGW(TAG, "Curve fitting calibration scheme is not supported on this chip");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to create curve fitting calibration scheme: %s", esp_err_to_name(ret));
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FMT_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = _adc_unit,
            .atten = _attenuation,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &_adc_cali_handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
        else if (ret == ESP_ERR_NOT_SUPPORTED || ret == ESP_ERR_INVALID_ARG)
        {
            ESP_LOGW(TAG, "Line fitting calibration scheme is not supported on this chip");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to create line fitting calibration scheme: %s", esp_err_to_name(ret));
        }
    }
#endif

    if (!calibrated)
    {
        ESP_LOGW(TAG, "No ADC calibration scheme is supported on this chip. Raw ADC values will be used.");
    }

    return calibrated;
}

void BatteryMonitor::_adc_calibration_deinit()
{
    if (_adc_cali_handle)
    {
#if ADC_CALI_SCHEME_CURVE_FMT_SUPPORTED
        ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(_adc_cali_handle));
#elif ADC_CALI_SCHEME_LINE_FMT_SUPPORTED
        ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
        ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(_adc_cali_handle));
#endif
        _adc_cali_handle = NULL;
    }
}

esp_err_t BatteryMonitor::init(adc_atten_t attenuation)
{
    _attenuation = attenuation;

    // ADC One-Shot 模式初始化
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = _adc_unit,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &_adc_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    // ADC 通道配置
    adc_oneshot_chan_cfg_t config = {
        .atten = _attenuation,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(_adc_handle, _adc_channel, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure ADC channel %d: %s", _adc_channel, esp_err_to_name(ret));
        adc_oneshot_del_unit(_adc_handle); // 清理资源
        _adc_handle = NULL;
        return ret;
    }

    // 初始化 ADC 校准
    _adc_calibration_is_init = _adc_calibration_init();

    ESP_LOGI(TAG, "BatteryMonitor initialized on ADC Unit %d, Channel %d with attenuation %d. Calibration %s.",
             _adc_unit, _adc_channel, _attenuation, _adc_calibration_is_init ? "enabled" : "disabled");

    return ESP_OK;
}

esp_err_t BatteryMonitor::read_voltage(float *voltage)
{
    if (!_adc_handle)
    {
        ESP_LOGE(TAG, "BatteryMonitor not initialized. Call init() first.");
        return ESP_ERR_INVALID_STATE;
    }
    if (!voltage)
    {
        ESP_LOGE(TAG, "Voltage pointer is NULL.");
        return ESP_ERR_INVALID_ARG;
    }

    int raw_adc;
    esp_err_t ret = adc_oneshot_read(_adc_handle, _adc_channel, &raw_adc);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read ADC channel %d: %s", _adc_channel, esp_err_to_name(ret));
        return ret;
    }

    uint32_t voltage_mv;
    int voltage_mv_int;

    if (_adc_calibration_is_init)
    {
        ret = adc_cali_raw_to_voltage(_adc_cali_handle, raw_adc, &voltage_mv_int);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to calibrate ADC raw value: %s. Falling back to estimation.", esp_err_to_name(ret));
            voltage_mv = _adc_raw_to_voltage_mv(raw_adc);
        } else {
            voltage_mv = voltage_mv_int / 1000.0f;
        }
    }
    else
    {
        voltage_mv = _adc_raw_to_voltage_mv(raw_adc);
    }

    *voltage = (float)voltage_mv / 1000.0f * _voltage_divider_ratio;

    ESP_LOGD(TAG, "Raw ADC: %d, Calibrated/Estimated mV: %lu, Final Voltage: %.2fV",
             raw_adc, voltage_mv, *voltage);

    return ESP_OK;
}

// 私有辅助函数，用于将原始 ADC 值转换为毫伏 (校准失败或不支持时的备用方案)
uint32_t BatteryMonitor::_adc_raw_to_voltage_mv(int raw_adc)
{
    // 这是一个非常粗略的估计，严重依赖于 Vref 和衰减。
    // 为了准确读数，强烈建议进行校准。
    // 对于 ESP32-C3，内部参考电压通常为 1100mV。
    // 衰减因子和相应的最大输入电压 (近似值):
    // ADC_ATTEN_DB_0:   ~1.1V (输入电压范围 0 ~ 1100mV)
    // ADC_ATTEN_DB_2_5: ~1.5V (输入电压范围 0 ~ 1500mV)
    // ADC_ATTEN_DB_6:   ~2.2V (输入电压范围 0 ~ 2200mV)
    // ADC_ATTEN_DB_11:  ~3.9V (输入电压范围 0 ~ 3900mV)

    uint32_t max_raw = (1 << ADC_BITWIDTH_DEFAULT) - 1; // 12 位分辨率通常为 4095

    uint32_t max_voltage_mv = 0;
    switch (_attenuation)
    {
    case ADC_ATTEN_DB_0:
        max_voltage_mv = 1100;
        break;
    case ADC_ATTEN_DB_2_5:
        max_voltage_mv = 1500;
        break;
    case ADC_ATTEN_DB_6:
        max_voltage_mv = 2200;
        break;
    case ADC_ATTEN_DB_11:
        max_voltage_mv = 3900;
        break;
    default:
        ESP_LOGW(TAG, "Unknown attenuation, using default max voltage for estimation (3.9V).");
        max_voltage_mv = 3900; // 备用方案，使用最高范围
        break;
    }

    return (uint32_t)((float)raw_adc / max_raw * max_voltage_mv);
}