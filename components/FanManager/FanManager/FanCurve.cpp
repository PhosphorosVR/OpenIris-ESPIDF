#include "FanCurve.hpp"

#ifdef CONFIG_FAN_PWM_ENABLE

#include <algorithm>

// Rev.5 tuning defaults. These only exist when revision detection is enabled;
// fall back so the curve stays compilable in every Kconfig combination.
#ifndef CONFIG_FAN_REV5_PWM_FREQ
#define CONFIG_FAN_REV5_PWM_FREQ 25000
#endif
#ifndef CONFIG_FAN_REV5_MIN_PERCENT
#define CONFIG_FAN_REV5_MIN_PERCENT 25
#endif

namespace
{
constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_10_BIT;

// 10-bit LEDC accepts 2^10 as "constantly high", so both ends of the range are
// reachable exactly: duty 0 is a steady low, duty 1024 a steady high.
constexpr uint32_t kDutyFull = 1u << kResolution;

/**
 * @brief Linearization LUT: user percent (0-100) -> raw 10-bit LEDC duty (0-1023).
 *
 * Compensates for the nonlinear PWM-to-motor-voltage characteristic of low-side
 * PWM fan drives.  Values are pre-computed from measured voltage data so that
 * equal user-percent steps produce approximately equal voltage steps.
 *
 * 10-bit resolution provides ~4x finer granularity than 8-bit, especially in
 * the critical low-PWM range where voltage changes most rapidly.
 *
 * Recalibrate by re-running the companion Python script with new measurements.
 */
// clang-format off
constexpr uint16_t kFanLinearizationLut[101] = {
    /*   0% */    0,   42,   44,   45,   46,   47,   49,   50,   51,   52,   53,
    /*  11% */   54,   54,   55,   56,   57,   57,   58,   59,   60,   61,   61,
    /*  22% */   63,   64,   66,   67,   69,   70,   72,   73,   75,   77,   78,
    /*  33% */   80,   81,   85,   89,   92,   94,   96,   98,  100,  102,  104,
    /*  44% */  106,  108,  110,  112,  115,  119,  123,  125,  128,  130,  132,
    /*  55% */  141,  146,  150,  154,  161,  167,  174,  180,  187,  193,  199,
    /*  66% */  207,  216,  226,  236,  246,  255,  267,  279,  292,  304,  321,
    /*  77% */  341,  360,  380,  399,  418,  438,  457,  477,  496,  517,  545,
    /*  88% */  573,  601,  628,  656,  684,  712,  754,  799,  844,  888,  933,
    /*  99% */  978, 1024
};
// clang-format on

/**
 * @brief Rev <= 4.5 - low-side MOSFET chopping the fan supply.
 *
 * Byte-for-byte the behaviour this firmware has always had: the same table, the
 * same lookup, the same Kconfig frequency and lower bound. Nothing here may
 * change without changing existing hardware in the field.
 */
class FanCurveLegacyLut final : public FanCurve
{
   public:
    uint32_t dutyForPercent(uint8_t percent) const override
    {
        return kFanLinearizationLut[std::min<uint8_t>(percent, 100)];
    }

    uint32_t frequencyHz() const override
    {
        return static_cast<uint32_t>(CONFIG_FAN_PWM_FREQ);
    }

    ledc_timer_bit_t resolution() const override
    {
        return kResolution;
    }

    uint32_t fullDuty() const override
    {
        return kDutyFull;
    }

    bool canTurnOff() const override
    {
        return true;
    }

    bool usesKickstart() const override
    {
        return false;
    }

    uint8_t defaultMinPercent() const override
    {
        return static_cast<uint8_t>(std::clamp(CONFIG_FAN_PWM_DUTY_MIN, 0, 100));
    }

    const char* name() const override
    {
        return "legacy-lut";
    }
};

/**
 * @brief Rev.5 - GPIO feeds an RC network into a linear regulator.
 *
 * The slope is negative and exactly linear, so no table is needed:
 *     U(duty) = 4.73 V - 2.00 V * duty
 * The user scale keeps its legacy meaning, linear in motor voltage:
 *     U(p)    = 2.73 V + 2.00 V * p / 100
 * Solving the two gives duty(p) = (100 - p) / 100.
 *
 * Consequences the rest of the system has to know about: 0 % is 2.73 V and not
 * off, and the RC filter needs a high switching frequency - 25 kHz leaves about
 * 33 mV of ripple, 20 kHz would leave 41 mV.
 */
class FanCurveRev5Linear final : public FanCurve
{
   public:
    uint32_t dutyForPercent(uint8_t percent) const override
    {
        const uint32_t p = std::min<uint32_t>(percent, 100);
        return (kDutyFull * (100u - p)) / 100u;
    }

    uint32_t frequencyHz() const override
    {
        return static_cast<uint32_t>(CONFIG_FAN_REV5_PWM_FREQ);
    }

    ledc_timer_bit_t resolution() const override
    {
        return kResolution;
    }

    uint32_t fullDuty() const override
    {
        return kDutyFull;
    }

    bool canTurnOff() const override
    {
        return false;
    }

    bool usesKickstart() const override
    {
        return true;
    }

    uint8_t defaultMinPercent() const override
    {
        return static_cast<uint8_t>(std::clamp(CONFIG_FAN_REV5_MIN_PERCENT, 0, 100));
    }

    const char* name() const override
    {
        return "rev5-linear";
    }
};

const FanCurveLegacyLut kLegacyCurve;
const FanCurveRev5Linear kRev5Curve;
}  // namespace

const FanCurve& fanCurveLegacyLut()
{
    return kLegacyCurve;
}

const FanCurve& fanCurveRev5Linear()
{
    return kRev5Curve;
}

#endif  // CONFIG_FAN_PWM_ENABLE
