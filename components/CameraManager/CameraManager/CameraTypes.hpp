#pragma once
#ifndef CAMERATYPES_HPP
#define CAMERATYPES_HPP

#include "sdkconfig.h"

#if CONFIG_CAMERA_STATUS
#include <cstdint>
#include <vector>
#include "esp_err.h"
#include "esp_system.h"

// Camera lifecycle as reported by get_camera_status (docs/CAM_CE_RESET_Analyse.md, section 10).
enum class CameraRunState : uint8_t
{
    Uninitialized,
    Starting,
    Running,
    Stopping,
    Off,
    Failed,
};

// Whether this board routes CAM_CE and CAM_RESET, decided once at boot.
enum class LinePresence : uint8_t
{
    Unknown,  // probe not run or not conclusive
    Present,  // lines reach the camera
    Absent,   // pads not connected: every operation is a silent no-op
};

enum class LineOutcome : uint8_t
{
    Done,          // pad driven as requested
    NotAvailable,  // board has no such line, pad left released
    Error,         // GPIO driver refused
};

// Result of the rail check (docs/CAM_CE_RESET_Analyse.md, section 5).
enum class RailVerdict : uint8_t
{
    NotChecked,
    Collapsed,     // rail below the end reading at both pads
    Partial,       // fell, but not below the collapse threshold
    NotCollapsed,  // did not fall (expected where the lines are absent)
    Inconclusive,  // no valid control reading, or the pads disagree
};

// Ordered by strength.
enum class RecoveryLevel : uint8_t
{
    Reinit,      // driver down and up again, camera stays powered
    HwReset,     // plus a CAM_RESET pulse
    PowerCycle,  // plus CAM_CE off, rail check
};

enum class RecoveryTrigger : uint8_t
{
    Bench,         // camera_power_cycle
    Command,       // recover_camera
    FrameTimeout,  // a consumer got no frame
    BootFailure,   // first camera init failed
};

struct CameraStatus
{
    CameraRunState state = CameraRunState::Uninitialized;
    esp_err_t init_result = ESP_OK;  // last esp_camera_init()
    uint16_t pid = 0;                // 0 until a sensor was detected
    esp_reset_reason_t reset_reason = ESP_RST_UNKNOWN;
    uint32_t frames_missing = 0;  // consumer requests that got no frame, since boot
};

struct CycleRequest
{
    RecoveryLevel level = RecoveryLevel::PowerCycle;
    uint32_t off_ms = 500;      // power cycle: CE low time, the minimum when adaptive
    bool adaptive_off = false;  // then hold CE low until the rail collapsed, up to 3 s
    bool trace = false;         // record the fall and rise of the rail
    bool force = false;         // drive the lines even where the probe found none
};

struct RailPoint
{
    uint32_t t_us;  // since CE low (fall) or CE release (rise)
    int16_t mv[2];
};

struct CycleReport
{
    RecoveryLevel level = RecoveryLevel::Reinit;
    LineOutcome power = LineOutcome::NotAvailable;
    LineOutcome reset = LineOutcome::NotAvailable;
    RailVerdict rail = RailVerdict::NotChecked;
    int control_mv[2] = {-1, -1};  // powered and in reset, before CE low
    int end_mv[2] = {-1, -1};      // end of the off time
    uint32_t off_ms = 0;           // measured CE low time
    int32_t rise_us = -1;          // CE release until both pads are back near control; -1 if not traced
    esp_err_t reinit = ESP_FAIL;
    uint16_t pid_before = 0;
    uint16_t pid_after = 0;
    bool first_frame = false;
    uint32_t first_frame_ms = 0;
    uint32_t duration_ms = 0;
    const char* failed_step = nullptr;  // nullptr on success
    uint32_t stack_free_min = 0;        // bytes left on the camera task
    std::vector<RailPoint> fall;
    std::vector<RailPoint> rise;
};

#if CONFIG_CAMERA_RECOVERY_ENABLE
struct RecoveryCounter
{
    uint32_t attempts = 0;
    uint32_t successes = 0;
};

// Compact record of one recovery, kept for get_camera_status.
struct RecoveryEntry
{
    uint32_t uptime_s = 0;
    RecoveryTrigger trigger = RecoveryTrigger::Command;
    RecoveryLevel level = RecoveryLevel::Reinit;
    RailVerdict rail = RailVerdict::NotChecked;
    const char* failed_step = nullptr;  // string literal, nullptr on success
    int8_t held = -1;                   // 1: frames kept coming for 30 s, 0: did not, -1: open
    uint32_t off_ms = 0;
    uint32_t first_frame_ms = 0;
    uint32_t duration_ms = 0;
};

enum class RecoveryRefusal : uint8_t
{
    Cooldown,
    Suspended,
    Busy,
    Count,
};

// Counters are the test result of an ESD run; they live until the next reset.
struct RecoveryStats
{
    static constexpr int kTriggers = 4;
    static constexpr int kLevels = 3;
    static constexpr int kVerdicts = 5;
    static constexpr int kLast = 4;

    RecoveryCounter by_trigger[kTriggers];  // index: RecoveryTrigger
    RecoveryCounter by_level[kLevels];      // index: RecoveryLevel
    uint32_t rail[kVerdicts] = {};          // index: RailVerdict
    uint32_t refused[static_cast<int>(RecoveryRefusal::Count)] = {};
    uint32_t held = 0;           // restarts after which frames kept coming for 30 s
    uint32_t not_held = 0;       // restarts that worked, but the camera was lost again before
    uint32_t unheld_in_row = 0;  // failed or not held, since the last one that held
    uint32_t suspensions = 0;
    uint32_t resumes = 0;                // suspensions lifted by the quiet time
    bool suspended = false;              // automatic triggers refused
    bool restarted_by_recovery = false;  // this boot follows a recovery ESP restart
#if CONFIG_CAMERA_RECOVERY_ESP_RESTART
    bool esp_restart_armed = false;  // false after such a restart until a start held
#endif
    RecoveryEntry last[kLast];           // ring, newest at (last_next - 1)
    uint8_t last_count = 0;
    uint8_t last_next = 0;
};

const char* recoveryRefusalName(RecoveryRefusal refusal);
#endif

const char* cameraRunStateName(CameraRunState state);
const char* resetReasonName(esp_reset_reason_t reason);
const char* linePresenceName(LinePresence presence);
const char* lineOutcomeName(LineOutcome outcome);
const char* railVerdictName(RailVerdict verdict);
const char* recoveryLevelName(RecoveryLevel level);
const char* recoveryTriggerName(RecoveryTrigger trigger);

#endif  // CONFIG_CAMERA_STATUS
#endif  // CAMERATYPES_HPP
