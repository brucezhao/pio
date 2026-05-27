#include "battery_monitor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string>
#include <cstdio>

class PowerManager {
private:
    BatteryMonitor batteryMonitor;
    bool usb_power_detected;
    const int USB_DETECT_PIN = GPIO_NUM_4;  // USB检测引脚
    
    // MQTT相关成员
    esp_mqtt_client_handle_t mqtt_client;
    bool mqtt_connected;
    const char* mqtt_broker_uri;
    const char* mqtt_topic;
    const char* mqtt_client_id;
    
    // WiFi配置
    const char* wifi_ssid;
    const char* wifi_password;

public:
    PowerManager() : 
        batteryMonitor(ADC_UNIT_1, ADC_CHANNEL_3, 2.0f), 
        usb_power_detected(false),
        mqtt_client(nullptr),
        mqtt_connected(false),
        mqtt_broker_uri("mqtt://broker.emqx.io"),  // 默认MQTT服务器
        mqtt_topic("esp32/battery/voltage"),  // 默认主题
        mqtt_client_id("mqttx_47ef22cd"),
        wifi_ssid("guanxin-24"),  // 替换为你的WiFi SSID
        wifi_password("Guanxinzhilian")  // 替换为你的WiFi密码
    {}

    void init() {
        // 初始化NVS（WiFi和MQTT需要）
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        
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
        
        // 初始化WiFi
        init_wifi();
        
        // 初始化MQTT
        init_mqtt();
    }
    
    void init_wifi() {
        const char * tag = "WIFI";
        ESP_LOGI(tag, "Initializing WiFi");
        
        // 初始化WiFi
        esp_netif_init();
        esp_event_loop_create_default();
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        
        // 配置WiFi
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.sta.ssid, wifi_ssid);
        strcpy((char*)wifi_config.sta.password, wifi_password);
        
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_start();
        
        // 等待WiFi连接
        esp_wifi_connect();
        ESP_LOGI(tag, "WiFi initialized, connecting to: %s", wifi_ssid);
    }
    
    void init_mqtt() {
        const char * tag = "MQTT";
        ESP_LOGI(tag, "Initializing MQTT client");
        
        // MQTT配置
        esp_mqtt_client_config_t mqtt_cfg = {};

        mqtt_cfg.broker.address.uri = mqtt_broker_uri;
        mqtt_cfg.session.keepalive = 120;
        
        // 创建MQTT客户端
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        
        // 注册MQTT事件处理
        esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, this);
        
        // 启动MQTT客户端
        esp_mqtt_client_start(mqtt_client);
        
        ESP_LOGI(tag, "MQTT client initialized, broker: %s", mqtt_broker_uri);
    }
    
    // MQTT事件处理函数
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, 
                                    int32_t event_id, void* event_data) {
        PowerManager* pm = static_cast<PowerManager*>(handler_args);
        const char * tag = "MQTT_EVENT";
        esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);
        
        switch (event->event_id) {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(tag, "MQTT connected");
                pm->mqtt_connected = true;
                break;
            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(tag, "MQTT disconnected");
                pm->mqtt_connected = false;
                break;
            case MQTT_EVENT_ERROR:
                ESP_LOGE(tag, "MQTT error");
                pm->mqtt_connected = false;
                break;
            default:
                break;
        }
    }
    
    // 通过MQTT发送电压数据
    void publish_voltage_mqtt(float voltage) {
        const char * tag = "MQTT_EVENT";
        if (!mqtt_connected || !mqtt_client) {
            ESP_LOGW(tag, "MQTT not connected, cannot publish voltage");
            return;
        }
        
        // 构建JSON消息
        char payload[128];
        int len = snprintf(payload, sizeof(payload),
                          "{\"voltage\": %.2f, \"usb_power\": %s}",
                          voltage,
                          usb_power_detected ? "true" : "false");
        
        if (len > 0 && len < sizeof(payload)) {
            // 发布到MQTT
            int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic, payload, 0, 1, 0);
            
            if (msg_id >= 0) {
                ESP_LOGI(tag, "Published voltage: %.2fV (msg_id: %d)", voltage, msg_id);
            } else {
                ESP_LOGE(tag, "Failed to publish voltage (msg_id: %d)", msg_id);
            }
        } else {
            ESP_LOGE(tag, "Failed to format payload");
        }
    }
    
    // 设置MQTT配置
    void set_mqtt_config(const char* broker_uri, const char* topic, const char* client_id = nullptr) {
        mqtt_broker_uri = broker_uri;
        mqtt_topic = topic;
        if (client_id != nullptr) {
            mqtt_client_id = client_id;
        }
    }
    
    // 设置WiFi配置
    void set_wifi_config(const char* ssid, const char* password) {
        wifi_ssid = ssid;
        wifi_password = password;
    }

    void update_usb_status() {
        // 实际检测 USB 是否连接
        usb_power_detected = (gpio_get_level(static_cast<gpio_num_t>(USB_DETECT_PIN)) == 1);
    }

    void print_power_status() {
        const char * tag = "POWER_STATUS";
        update_usb_status();  // 先更新状态

        float voltage;
        if (batteryMonitor.read_voltage(&voltage) == ESP_OK) {
            if (usb_power_detected) {
                ESP_LOGI(tag, "USB Powered - Battery voltage: %.2fV", voltage);
            } else {
                ESP_LOGI(tag, "Battery Powered - Voltage: %.2fV", voltage);
            }
            
            // 通过MQTT发送电压
            publish_voltage_mqtt(voltage);
        }
    }
    
    // 获取当前电压值
    float get_voltage() {
        float voltage = 0.0f;
        batteryMonitor.read_voltage(&voltage);
        return voltage;
    }
};

// 使用示例
extern "C" void app_main() {
    PowerManager pm;
    // pm.set_mqtt_config("mqtt://broker.emqx.io", "esp32/battery/voltage", "my_custom_client_id");
    for (int i = 0; i < 10; i++) {
        ESP_LOGI("main", "Loop iteration: %d", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    pm.init();

    while (1) {
        pm.print_power_status();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}