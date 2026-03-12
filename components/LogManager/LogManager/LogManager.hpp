#pragma once
#ifndef LOGMANAGER_HPP
#define LOGMANAGER_HPP

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "esp_log.h"
#include "sdkconfig.h"

#ifdef CONFIG_DEBUG_LOG_ENABLE

struct LogEntry
{
    int64_t timestamp_ms;
    esp_log_level_t level;
    char message[200];
};

class LogManager
{
   public:
    LogManager() = default;
    ~LogManager() = default;

    void setup();
    void start();
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Retrieve current session logs from RAM ringbuffer
    std::vector<LogEntry> getRecentLogs() const;

    // Retrieve persistent logs from SPIFFS (last N boots)
    std::string getPersistentLogs() const;
    bool clearPersistentLogs();

    // Custom vprintf hook – called by esp_log
    static int logHook(const char* format, va_list args);

   private:
    void mountSpiffs();
    void rotateLogs();
    void flushPendingLogs();

    static void flushTaskEntry(void* arg);
    void flushLoop();

    mutable std::mutex ring_mutex_;
    LogEntry ring_[CONFIG_DEBUG_LOG_RINGBUFFER_SIZE]{};
    size_t ring_head_{0};
    size_t ring_count_{0};

    // Pending persistent entries waiting to be written to flash
    mutable std::mutex pending_mutex_;
    std::vector<LogEntry> pending_;

    bool spiffs_mounted_{false};
    std::atomic_bool enabled_{true};

    // Original vprintf so we still see output on default console
    vprintf_like_t original_vprintf_{nullptr};
};

extern LogManager logManager;

#endif  // CONFIG_DEBUG_LOG_ENABLE
#endif  // LOGMANAGER_HPP
