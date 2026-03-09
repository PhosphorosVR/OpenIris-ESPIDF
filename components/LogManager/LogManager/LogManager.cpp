#include "LogManager.hpp"

#ifdef CONFIG_DEBUG_LOG_ENABLE

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "[LogManager]";

LogManager logManager;

// ---------- SPIFFS helpers ----------

void LogManager::mountSpiffs()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/logs",
        .partition_label = "spiffs",
        .max_files = CONFIG_DEBUG_LOG_PERSISTENT_BOOTS + 1,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK)
    {
        spiffs_mounted_ = true;
        ESP_LOGI(TAG, "SPIFFS mounted for persistent logs");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
    }
}

void LogManager::rotateLogs()
{
    if (!spiffs_mounted_)
        return;

    const int max_boots = CONFIG_DEBUG_LOG_PERSISTENT_BOOTS;

    // Delete the oldest file
    char path[32];
    snprintf(path, sizeof(path), "/logs/log_%d.txt", max_boots - 1);
    remove(path);

    // Shift remaining files: log_N-2 -> log_N-1, ... , log_0 -> log_1
    for (int i = max_boots - 2; i >= 0; i--)
    {
        char src[32];
        char dst[32];
        snprintf(src, sizeof(src), "/logs/log_%d.txt", i);
        snprintf(dst, sizeof(dst), "/logs/log_%d.txt", i + 1);
        rename(src, dst);
    }

    // Create empty current log file
    FILE* f = fopen("/logs/log_0.txt", "w");
    if (f)
    {
        fclose(f);
    }
}

// ---------- RAM ringbuffer ----------

static const char* levelToStr(esp_log_level_t level)
{
    switch (level)
    {
    case ESP_LOG_ERROR:
        return "E";
    case ESP_LOG_WARN:
        return "W";
    default:
        return "?";
    }
}

static esp_log_level_t detectLevel(const char* msg)
{
    // ESP_LOG format: "\033[0;3Xm<tag>" or "E (<timestamp>)" / "W (<timestamp>)"
    if (!msg)
        return ESP_LOG_NONE;

    // Check for ANSI color codes used by ESP_LOG
    const char* p = msg;
    // Skip leading ANSI escape if present
    if (p[0] == '\033' && p[1] == '[')
    {
        // \033[0;31m = RED = ERROR, \033[0;33m = YELLOW = WARN
        if (strstr(p, "\033[0;31m"))
            return ESP_LOG_ERROR;
        if (strstr(p, "\033[0;33m"))
            return ESP_LOG_WARN;
    }

    // Plain text fallback: "E (12345) tag: ..."
    if (p[0] == 'E' && p[1] == ' ')
        return ESP_LOG_ERROR;
    if (p[0] == 'W' && p[1] == ' ')
        return ESP_LOG_WARN;

    return ESP_LOG_NONE;
}

int LogManager::logHook(const char* format, va_list args)
{
    // Always forward to original output first
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = logManager.original_vprintf_(format, args_copy);
    va_end(args_copy);

    // Format the message to check its level
    char buf[200];
    vsnprintf(buf, sizeof(buf), format, args);

    esp_log_level_t level = detectLevel(buf);
    if (level != ESP_LOG_ERROR && level != ESP_LOG_WARN)
        return ret;

    int64_t now = esp_timer_get_time() / 1000;

    LogEntry entry{};
    entry.timestamp_ms = now;
    entry.level = level;
    // Store the message without ANSI escape codes for cleaner output
    const char* clean = buf;
    if (buf[0] == '\033')
    {
        // Skip past the escape sequence: \033[X;XXm
        const char* m = strchr(buf, 'm');
        if (m)
            clean = m + 1;
    }
    // Also trim trailing reset sequence \033[0m
    size_t len = strlen(clean);
    char tmp[200];
    strncpy(tmp, clean, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* reset = strstr(tmp, "\033[0m");
    if (reset)
        *reset = '\0';
    // Trim trailing newline
    len = strlen(tmp);
    while (len > 0 && (tmp[len - 1] == '\n' || tmp[len - 1] == '\r'))
        tmp[--len] = '\0';

    strncpy(entry.message, tmp, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    // Write to RAM ringbuffer
    {
        std::lock_guard<std::mutex> lock(logManager.ring_mutex_);
        logManager.ring_[logManager.ring_head_] = entry;
        logManager.ring_head_ = (logManager.ring_head_ + 1) % CONFIG_DEBUG_LOG_RINGBUFFER_SIZE;
        if (logManager.ring_count_ < CONFIG_DEBUG_LOG_RINGBUFFER_SIZE)
            logManager.ring_count_++;
    }

    // Queue for persistent storage
    {
        std::lock_guard<std::mutex> lock(logManager.pending_mutex_);
        logManager.pending_.push_back(entry);
    }

    return ret;
}

// ---------- Persistent flush ----------

void LogManager::flushPendingLogs()
{
    if (!spiffs_mounted_)
        return;

    std::vector<LogEntry> to_write;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (pending_.empty())
            return;
        to_write.swap(pending_);
    }

    FILE* f = fopen("/logs/log_0.txt", "a");
    if (!f)
        return;

    for (const auto& e : to_write)
    {
        fprintf(f, "[%lld] %s %s\n", (long long)e.timestamp_ms, levelToStr(e.level), e.message);
    }
    fclose(f);
}

void LogManager::flushTaskEntry(void* arg)
{
    static_cast<LogManager*>(arg)->flushLoop();
}

void LogManager::flushLoop()
{
    const TickType_t interval = pdMS_TO_TICKS(CONFIG_DEBUG_LOG_FLUSH_INTERVAL_MS);
    while (true)
    {
        vTaskDelay(interval);
        flushPendingLogs();
    }
}

// ---------- Public API ----------

void LogManager::setup()
{
    mountSpiffs();
    rotateLogs();
    ESP_LOGI(TAG, "Log capture enabled, ringbuffer=%d entries, persistent boots=%d",
             CONFIG_DEBUG_LOG_RINGBUFFER_SIZE, CONFIG_DEBUG_LOG_PERSISTENT_BOOTS);
}

void LogManager::start()
{
    // Hook into esp_log output
    original_vprintf_ = esp_log_set_vprintf(&LogManager::logHook);

    xTaskCreate(&LogManager::flushTaskEntry, "LogFlush", 1024 * 3, this, 1, nullptr);
    ESP_LOGI(TAG, "Log capture started, flush interval=%d ms", CONFIG_DEBUG_LOG_FLUSH_INTERVAL_MS);
}

std::vector<LogEntry> LogManager::getRecentLogs() const
{
    std::lock_guard<std::mutex> lock(ring_mutex_);
    std::vector<LogEntry> result;
    result.reserve(ring_count_);

    if (ring_count_ == 0)
        return result;

    // Return oldest-first
    size_t start = (ring_count_ < CONFIG_DEBUG_LOG_RINGBUFFER_SIZE) ? 0 : ring_head_;
    for (size_t i = 0; i < ring_count_; i++)
    {
        size_t idx = (start + i) % CONFIG_DEBUG_LOG_RINGBUFFER_SIZE;
        result.push_back(ring_[idx]);
    }
    return result;
}

std::string LogManager::getPersistentLogs() const
{
    if (!spiffs_mounted_)
        return "SPIFFS not mounted";

    // Flush any pending entries before reading
    const_cast<LogManager*>(this)->flushPendingLogs();

    std::string result;
    const int max_boots = CONFIG_DEBUG_LOG_PERSISTENT_BOOTS;

    // Read from oldest to newest
    for (int i = max_boots - 1; i >= 0; i--)
    {
        char path[32];
        snprintf(path, sizeof(path), "/logs/log_%d.txt", i);

        FILE* f = fopen(path, "r");
        if (!f)
            continue;

        result += "--- boot -";
        result += std::to_string(i);
        result += " ---\n";

        char line[256];
        while (fgets(line, sizeof(line), f))
        {
            result += line;
        }
        fclose(f);
    }

    if (result.empty())
        result = "No persistent logs available";

    return result;
}

#endif  // CONFIG_DEBUG_LOG_ENABLE
