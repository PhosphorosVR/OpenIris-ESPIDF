#pragma once
#ifndef _FANCURVE_HPP_
#define _FANCURVE_HPP_

#include "sdkconfig.h"

#ifdef CONFIG_FAN_PWM_ENABLE

#include <cstdint>
#include "driver/ledc.h"

/**
 * @brief A fan drive characteristic: user percent -> raw LEDC duty.
 *
 * One implementation per board generation, selected once at boot and never
 * swapped afterwards. Curves are pure and const; every value an operator can
 * change lives in FanManager, so a curve stays trivially inspectable.
 *
 * The user scale is defined to be linear in motor voltage, not in duty. That
 * is how the legacy LUT was generated (tools/fan_calibration.py) and it is why
 * the two curves produce the same voltage for the same percentage from 1 %
 * upwards. At 0 % they diverge completely: legacy switches the fan off, Rev.5
 * cannot.
 */
class FanCurve
{
   public:
    virtual ~FanCurve() = default;

    /// Raw LEDC duty for a user percentage. 0 % means "as slow as this board
    /// goes", which is not the same as "off" on every generation.
    virtual uint32_t dutyForPercent(uint8_t percent) const = 0;

    /// Timer setup belongs to the curve: the Rev.5 RC network needs a high
    /// frequency to keep the ripple down, the MOSFET gate does not care.
    virtual uint32_t frequencyHz() const = 0;
    virtual ledc_timer_bit_t resolution() const = 0;

    /// Highest raw duty this curve's resolution accepts (constant high).
    virtual uint32_t fullDuty() const = 0;

    /// True when 0 % physically stops the fan.
    virtual bool canTurnOff() const = 0;

    /// True when the fan may sit below its start-up threshold and needs a boost
    /// before it can be trusted to spin. False for legacy, which keeps that
    /// path from ever running on existing hardware.
    virtual bool usesKickstart() const = 0;

    /// Lowest percentage this generation may be driven at by default. Legacy
    /// takes CONFIG_FAN_PWM_DUTY_MIN, Rev.5 its own value - keeping the two
    /// apart here is what stops a Rev.5 default from leaking onto old boards.
    virtual uint8_t defaultMinPercent() const = 0;

    virtual const char* name() const = 0;
};

/// Rev <= 4.5: low-side MOSFET, the measured linearization LUT, unchanged.
const FanCurve& fanCurveLegacyLut();

/// Rev.5: RC network into a linear regulator, inverted and exactly linear.
const FanCurve& fanCurveRev5Linear();

#endif  // CONFIG_FAN_PWM_ENABLE
#endif  // _FANCURVE_HPP_
