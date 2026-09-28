#pragma once
#ifndef RAILSENSE_HPP
#define RAILSENSE_HPP

#include "sdkconfig.h"

#if CONFIG_CAMERA_RAIL_SENSE
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"

// Upper bound for the camera rail (2V8_Cx) while the camera is off.
// Two DVP data pads are read over ADC2 with the internal pull-up on. A camera output
// PMOS clamps its pad to the rail plus one diode drop, so a reading well below the
// powered reading of the same cycle proves the rail is below that reading. A missing
// or different pad structure can only keep the reading high, never fake a collapse.
class RailSense
{
   public:
    static constexpr int kPads = 2;

    RailSense() = default;
    ~RailSense();
    RailSense(const RailSense&) = delete;
    RailSense& operator=(const RailSense&) = delete;

    // Pads into the ADC, pulled down between readings. The camera must be off or in reset.
    bool begin();
    // One reading per pad with the pull-up on, averaged over `samples`; -1 on failure.
    bool measure(int (&mv)[kPads], int samples);
    // Pads back to plain digital inputs; must run before the camera driver takes them again.
    void end();

   private:
    adc_oneshot_unit_handle_t unit_ = nullptr;
    adc_channel_t channel_[kPads]{};
    adc_cali_handle_t cali_[kPads]{};
    bool active_ = false;  // pads touched, end() required
    bool ready_ = false;   // measure() possible
};

#else
// Without the option nothing is measured and every rail verdict stays "not_checked".
class RailSense
{
   public:
    static constexpr int kPads = 2;
    bool begin()
    {
        return false;
    }
    bool measure(int (&)[kPads], int)
    {
        return false;
    }
    void end() {}
};
#endif  // CONFIG_CAMERA_RAIL_SENSE
#endif  // RAILSENSE_HPP
