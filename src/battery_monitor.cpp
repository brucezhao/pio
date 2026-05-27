#include "battery_monitor.h"
#include "esp_log.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "BatteryMonitor";

BatteryMonitor::BatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel, float voltage_divider_ratio)
    : _adc_unit(adc_unit), _adc_channel(adc_channel), _voltage_divider_ratio(voltage_divider_ratio),
      _adc_handle(NULL), _attenuation(ADC_ATTEN_DB_12), _adc_cali_handle(NULL), _adc_calibration_is_init(false)
{
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

    // ESP32-C3 只支持 Line Fitting 校准方案
#if ADC_CALI_SCHEME_LINE_FMT_SUPPORTED
    ESP_LOGI(TAG, "Using Line Fitting calibration scheme");
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = _adc_unit,
        .atten = _attenuation,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &_adc_cali_handle);
    if (ret == ESP_OK)
    {
        calibrated = true;
        ESP_LOGI(TAG, "ADC calibration initialized successfully");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED)
    {
        ESP_LOGW(TAG, "Line fitting calibration is not supported on this chip");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to create line fitting calibration: %s", esp_err_to_name(ret));
    }
#endif

    if (!calibrated)
    {
        ESP_LOGW(TAG, "ADC calibration failed. Raw ADC values will be used with voltage estimation.");
    }

    return calibrated;
}

void BatteryMonitor::_adc_calibration_deinit()
{
    if (_adc_cali_handle)
    {
#if ADC_CALI_SCHEME_LINE_FMT_SUPPORTED
        ESP_LOGI(TAG, "Deinitializing ADC calibration");
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
        adc_oneshot_del_unit(_adc_handle);
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

    int voltage_mv = 0;

    if (_adc_calibration_is_init)
    {
        ret = adc_cali_raw_to_voltage(_adc_cali_handle, raw_adc, &voltage_mv);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to calibrate ADC raw value: %s. Using estimation.", esp_err_to_name(ret));
            voltage_mv = _adc_raw_to_voltage_mv(raw_adc);
        }
    }
    else
    {
        voltage_mv = _adc_raw_to_voltage_mv(raw_adc);
    }

    *voltage = (float)voltage_mv / 1000.0f * _voltage_divider_ratio;

    ESP_LOGD(TAG, "Raw ADC: %d (%d mV), Final Voltage: %.2fV",
             raw_adc, voltage_mv, *voltage);

    return ESP_OK;
}

uint32_t BatteryMonitor::_adc_raw_to_voltage_mv(int raw_adc)
{
    // 对于 ESP32-C3 的改进估算
    // ESP32-C3 的参考电压典型值为 1100mV
    const uint32_t vref_mv = 1100;
    uint32_t max_raw = (1 << ADC_BITWIDTH_DEFAULT) - 1; // 4095 for 12-bit

    // 根据衰减计算最大电压
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
    case ADC_ATTEN_DB_12:  // 注意：ESP32-C3 使用 DB_12 而不是 DB_11
        max_voltage_mv = 3300;  // ESP32-C3 在 12dB 衰减时最大约 3.3V
        break;
    default:
        max_voltage_mv = 3300;
        break;
    }

    // 线性估算
    return (uint32_t)((float)raw_adc / max_raw * max_voltage_mv);
}