#pragma once
#ifndef CAMLINES_HPP
#define CAMLINES_HPP

#include "sdkconfig.h"

#if CONFIG_CAMERA_POWER_CONTROL
#include <cstdint>

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

struct LineProbe
{
    LinePresence presence = LinePresence::Unknown;
    int reset_pullup_mv = -1;  // reset node with the internal pull-up
    int reset_pulldown_mv = -1;  // reset node with the internal pull-down
    int ce_mv = -1;  // CE node without pulls, plausibility only
};

// CAM_CE (enable of both camera LDOs) and CAM_RESET as open drain: pulled low or
// released, never driven high. Released is the power-on state on every revision,
// so a reset of the ESP always leaves the camera powered and out of reset.
class CamLines
{
   public:
    // Boot only, before the first camera init. Leaves both lines released.
    const LineProbe& probe();

    const LineProbe& lastProbe() const
    {
        return probe_;
    }
    bool available() const
    {
        return probe_.presence == LinePresence::Present;
    }

    // Without force a line the probe did not find is left alone (NotAvailable).
    // Releasing always happens, it is the safe state.
    LineOutcome powerOff(bool force = false);
    LineOutcome powerOn(bool force = false);
    LineOutcome holdReset(bool force = false);
    LineOutcome releaseReset(bool force = false);

    bool powerHeldOff() const
    {
        return power_off_;
    }
    bool resetHeld() const
    {
        return reset_held_;
    }

   private:
    LineProbe probe_{};
    bool power_off_ = false;
    bool reset_held_ = false;
};

const char* linePresenceName(LinePresence presence);
const char* lineOutcomeName(LineOutcome outcome);

#endif  // CONFIG_CAMERA_POWER_CONTROL
#endif  // CAMLINES_HPP
