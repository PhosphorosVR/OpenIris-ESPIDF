#include "WiFiScanner.hpp"
#include <cstring>
#include "esp_timer.h"

static const char* TAG = "WiFiScanner";

WiFiScanner::WiFiScanner() {}

std::vector<WiFiNetwork> WiFiScanner::scanNetworks(int timeout_ms)
{
    std::vector<WiFiNetwork> scan_results;

    // Check if WiFi is initialized
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err == ESP_ERR_WIFI_NOT_INIT)
    {
        ESP_LOGE(TAG, "WiFi not initialized");
        return scan_results;
    }

    // Give WiFi more time to be ready
    vTaskDelay(pdMS_TO_TICKS(500));

    // Stop any ongoing scan
    esp_wifi_scan_stop();

    // Sequential channel scan - scan each channel individually with timeout tracking
    std::vector<wifi_ap_record_t> all_records;
    int64_t start_time = esp_timer_get_time() / 1000;  // Convert to ms

    for (uint8_t ch = 1; ch <= 13; ch++)
    {
        // Check if we've exceeded the timeout
        int64_t current_time = esp_timer_get_time() / 1000;
        int64_t elapsed = current_time - start_time;

        if (elapsed >= timeout_ms)
        {
            ESP_LOGW(TAG, "Sequential scan timeout after %lld ms at channel %d", elapsed, ch);
            break;
        }

        wifi_scan_config_t scan_config = {.ssid = nullptr,
                                          .bssid = nullptr,
                                          .channel = ch,
                                          .show_hidden = true,
                                          .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                                          .scan_time = {.active = {.min = 100, .max = 200}, .passive = 300},
                                          .home_chan_dwell_time = 0,
                                          .channel_bitmap = 0};

        err = esp_wifi_scan_start(&scan_config, true);  // Blocking scan
        if (err == ESP_OK)
        {
            uint16_t ch_count = 0;
            esp_wifi_scan_get_ap_num(&ch_count);
            if (ch_count > 0)
            {
                wifi_ap_record_t* ch_records = new wifi_ap_record_t[ch_count];
                if (esp_wifi_scan_get_ap_records(&ch_count, ch_records) == ESP_OK)
                {
                    for (uint16_t i = 0; i < ch_count; i++)
                    {
                        all_records.push_back(ch_records[i]);
                    }
                }
                delete[] ch_records;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Process all collected records
    for (const auto& record : all_records)
    {
        WiFiNetwork network;
        network.ssid = std::string(reinterpret_cast<const char*>(record.ssid));
        network.channel = record.primary;
        network.rssi = record.rssi;
        memcpy(network.mac, record.bssid, 6);
        network.auth_mode = record.authmode;
        scan_results.push_back(network);
    }

    int64_t total_time = (esp_timer_get_time() / 1000) - start_time;
    ESP_LOGI(TAG, "Sequential scan completed in %lld ms, found %d APs", total_time, scan_results.size());

    // Skip the normal result processing
    return scan_results;
}