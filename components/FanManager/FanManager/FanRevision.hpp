#pragma once
#ifndef _FANREVISION_HPP_
#define _FANREVISION_HPP_

#include "driver/gpio.h"
#include "sdkconfig.h"

/**
 * @brief Which fan drive circuit is on the board.
 *
 * This is a measurement of the fan pin, not of the PCB as a whole - it happens
 * to identify the board generation because that is where the two circuits
 * differ. Unknown means the node behaved like neither, i.e. something is broken.
 */
enum class FanRevision
{
    Legacy,   ///< Rev <= 4.5: low-side MOSFET gate, ~1 nF against 100 k
    Rev5,     ///< Rev.5: 1 uF RC network into a linear regulator
    Unknown,  ///< neither - the caller must fall back to Legacy
};

struct FanRevisionResult
{
    FanRevision revision = FanRevision::Unknown;
    bool measured = false;   ///< false when detection is compiled out
    bool used_adc = false;   ///< false when the digital fallback ran
    int sample_a_mv = -1;    ///< 2.5 ms after release, -1 if not measured
    int sample_b_mv = -1;    ///< ~30 ms after release, -1 if not measured
    int release_to_a_us = 0; ///< actual delay achieved, for diagnosis
};

const char* fanRevisionName(FanRevision revision);

/**
 * @brief Identify the fan drive circuit by watching the pin discharge.
 *
 * Charges the fan node, lets go, and samples the decay twice. Rev.5 carries
 * 1 uF and decays with tau = 35..80 ms; Rev <= 4.5 carries only a MOSFET gate
 * and collapses within 150 us. Sample A alone classifies - it sits in the
 * middle of a 3 V gap. Sample B only proves the node actually decays, which is
 * what separates a healthy Rev.5 board from one shorted to the rail.
 *
 * @note The pin is left disabled with both pulls off. The caller owns it again
 *       and can hand it straight to LEDC.
 * @note Briefly drives the pin high, which turns the MOSFET on for the charge
 *       window on Rev <= 4.5. Too short to move the fan, but not nothing.
 */
FanRevisionResult detectFanRevision(gpio_num_t pin);

#endif  // _FANREVISION_HPP_
