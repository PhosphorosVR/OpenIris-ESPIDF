#include "WiFiScanner.hpp"
#include <cstring>
#include "esp_timer.h"

static const char *TAG = "WiFiScanner";

WiFiScanner::WiFiScanner() {}

void WiFiScanner::scanResultCallback(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data)
{
    auto *scanner = static_cast<WiFiScanner *>(arg);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);

        if (ap_count == 0)
        {
            ESP_LOGI(TAG, "No access points found");
            return;
        }

        wifi_ap_record_t *ap_records = new wifi_ap_record_t[ap_count];
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

        scanner->networks.clear();
        for (uint16_t i = 0; i < ap_count; i++)
        {
            WiFiNetwork network;
            network.ssid = std::string(reinterpret_cast<char *>(ap_records[i].ssid));
            network.channel = ap_records[i].primary;
            network.rssi = ap_records[i].rssi;
            memcpy(network.mac, ap_records[i].bssid, 6);
            network.auth_mode = ap_records[i].authmode;

            scanner->networks.push_back(network);
        }

        delete[] ap_records;
        ESP_LOGI(TAG, "Found %d access points", ap_count);
    }
}

// todo this is garbage
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

    // Try sequential channel scanning as a workaround
    bool try_sequential_scan = true; // Enable sequential scan

    if (!try_sequential_scan)
    {
        // Normal all-channel scan
        wifi_scan_config_t scan_config = {
            .ssid = nullptr,
            .bssid = nullptr,
            .channel = 0, // 0 means scan all channels
            .show_hidden = true,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE, // Active scan
            .scan_time = {
                .active = {
                    .min = 120, // Min per channel
                    .max = 300  // Max per channel
                },
                .passive = 360},
            .home_chan_dwell_time = 0, // 0 for default
            .channel_bitmap = 0        // 0 for all channels
        };

        err = esp_wifi_scan_start(&scan_config, false);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));
            return scan_results;
        }
    }
    else
    {
        // Sequential channel scan - scan each channel individually with timeout tracking
        std::vector<wifi_ap_record_t> all_records;
        int64_t start_time = esp_timer_get_time() / 1000; // Convert to ms

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

            wifi_scan_config_t scan_config = {
                .ssid = nullptr,
                .bssid = nullptr,
                .channel = ch,
                .show_hidden = true,
                .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                .scan_time = {
                    .active = {
                        .min = 100,
                        .max = 200},
                    .passive = 300},
                .home_chan_dwell_time = 0,
                .channel_bitmap = 0};

            err = esp_wifi_scan_start(&scan_config, true); // Blocking scan
            if (err == ESP_OK)
            {
                uint16_t ch_count = 0;
                esp_wifi_scan_get_ap_num(&ch_count);
                if (ch_count > 0)
                {
                    wifi_ap_record_t *ch_records = new wifi_ap_record_t[ch_count];
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
        for (const auto &record : all_records)
        {
            WiFiNetwork network;
            network.ssid = std::string(reinterpret_cast<const char *>(record.ssid));
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

    // Wait for scan completion with timeout
    int64_t start_time = esp_timer_get_time() / 1000; // Convert to ms
    int64_t elapsed_ms = 0;
    bool scan_done = false;

    while (elapsed_ms < timeout_ms)
    {
        // Check if scan is actually complete by trying to get AP count
        // When scan is done, this will return ESP_OK with a valid count
        uint16_t temp_count = 0;
        esp_err_t count_err = esp_wifi_scan_get_ap_num(&temp_count);

        // If we can successfully get the AP count, the scan is likely complete
        // However, we should still wait for the scan to fully finish
        if (count_err == ESP_OK && temp_count > 0)
        {
            // Give it a bit more time to ensure all channels are scanned
            vTaskDelay(pdMS_TO_TICKS(500));
            scan_done = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
        elapsed_ms = (esp_timer_get_time() / 1000) - start_time;
    }

    if (!scan_done && elapsed_ms >= timeout_ms)
    {
        ESP_LOGE(TAG, "Scan timeout after %lld ms", elapsed_ms);
        esp_wifi_scan_stop();
        return scan_results;
    }

    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0)
    {
        ESP_LOGI(TAG, "No access points found");
        return scan_results;
    }

    wifi_ap_record_t *ap_records = new wifi_ap_record_t[ap_count];
    err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get scan records: %s", esp_err_to_name(err));
        delete[] ap_records;
        return scan_results;
    }

    // Build the results vector and track channels found
    bool channels_found[15] = {false}; // Track channels 0-14

    for (uint16_t i = 0; i < ap_count; i++)
    {
        WiFiNetwork network;
        network.ssid = std::string(reinterpret_cast<char *>(ap_records[i].ssid));
        network.channel = ap_records[i].primary;
        network.rssi = ap_records[i].rssi;
        memcpy(network.mac, ap_records[i].bssid, 6);
        network.auth_mode = ap_records[i].authmode;

        if (network.channel <= 14)
        {
            channels_found[network.channel] = true;
        }

        scan_results.push_back(network);
    }

    delete[] ap_records;
    ESP_LOGI(TAG, "Found %d access points", ap_count);

    return scan_results;
}