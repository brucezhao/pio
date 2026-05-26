#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {

int app_main() {
    const char * TAG = "MAIN";

    for (;;) {
        ESP_LOGI(TAG, "Hello from ESP-IDF");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

}