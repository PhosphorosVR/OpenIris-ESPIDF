"""
Fan PWM Linearization LUT Generator
====================================

Generates a lookup table (LUT) that maps a linear user percentage (0-100%)
to raw LEDC duty values, compensating for the nonlinear PWM-to-voltage
characteristic of low-side PWM fan drives.

Usage:
------
1. Measure the effective motor voltage at various PWM duty cycles.
   Use a multimeter (DC mode) across the motor terminals while the
   fan is running at each duty cycle.

2. Enter your measurements in the MEASUREMENTS list below as tuples
   of (pwm_percent, voltage_millivolts).
   - Always include (0, 0) and (100, <your_max_voltage>).
   - Focus on the low PWM range (2-20%) where the curve is steepest.
   - At least 10-15 points are recommended for a good fit.

3. Set RESOLUTION_BITS to match your LEDC timer resolution (8, 10, or 11).

4. Run the script:
       python tools/fan_calibration.py

5. Copy the generated C++ array into FanManager.cpp.

   Do NOT override existing calibration profiles.  If you need a different
   one (e.g. for another fan or board), create a LUT profile system instead
   of replacing the current table.

Example measurements (PWM% -> motor voltage in mV):
    (0, 0), (4, 2750), (5, 2900), (10, 3550), (20, 4000), (100, 4650)
"""

import sys

# =============================================================================
#  USER CONFIGURATION — Edit these values with your measurements
# =============================================================================

# (pwm_percent, measured_voltage_in_millivolts)
# IMPORTANT: Must be sorted by pwm_percent ascending.
#            Must include (0, 0) as the first entry.
MEASUREMENTS = [
    (0, 0),
    (4, 2750),
    (5, 2900),
    (6, 3150),
    (7, 3280),
    (8, 3400),
    (9, 3450),
    (10, 3550),
    (11, 3650),
    (12, 3700),
    (13, 3780),
    (14, 3800),
    (15, 3850),
    (20, 4000),
    (25, 4100),
    (30, 4180),
    (50, 4380),
    (70, 4520),
    (100, 4650),
]

# LEDC timer resolution in bits (8 = 256 steps, 10 = 1024, 11 = 2048)
RESOLUTION_BITS = 10

# =============================================================================
#  END OF USER CONFIGURATION
# =============================================================================


def validate_measurements(data: list[tuple[int, int]]) -> None:
    """Sanity-check the measurement data."""
    if len(data) < 3:
        print("ERROR: Need at least 3 measurement points.", file=sys.stderr)
        sys.exit(1)

    if data[0] != (0, 0):
        print("ERROR: First measurement must be (0, 0).", file=sys.stderr)
        sys.exit(1)

    for i in range(1, len(data)):
        if data[i][0] <= data[i - 1][0]:
            print(
                f"ERROR: Measurements must be sorted by PWM% ascending. "
                f"Entry {i} ({data[i][0]}%) <= entry {i-1} ({data[i-1][0]}%).",
                file=sys.stderr,
            )
            sys.exit(1)
        if data[i][1] < data[i - 1][1]:
            print(
                f"WARNING: Voltage at {data[i][0]}% ({data[i][1]} mV) is lower "
                f"than at {data[i-1][0]}% ({data[i-1][1]} mV). "
                f"Check your measurements.",
                file=sys.stderr,
            )


def generate_lut(
    data: list[tuple[int, int]], resolution_bits: int
) -> list[int]:
    """
    Generate a 101-entry LUT mapping user percent (0-100) to raw LEDC duty.

    For user 0% the output is always 0 (off).
    For user 1-100% the target voltage is linearly interpolated between
    V_min (lowest nonzero measurement) and V_max (highest measurement).
    The required PWM duty is then found by inverse-interpolating the
    measured voltage curve.
    """
    max_duty = (1 << resolution_bits) - 1  # e.g. 1023 for 10-bit

    pwm_raw = [d[0] * max_duty / 100.0 for d in data]
    v_vals = [d[1] for d in data]

    # Voltage range: from first nonzero measurement to last
    v_min = next(v for _, v in data if v > 0)
    v_max = v_vals[-1]

    lut = [0]  # user 0% -> off

    for user_pct in range(1, 101):
        target_v = v_min + user_pct * (v_max - v_min) / 100.0

        # Inverse interpolation: find PWM duty for target voltage
        duty = float(max_duty)
        for i in range(1, len(data)):
            if v_vals[i] >= target_v:
                v0, v1 = v_vals[i - 1], v_vals[i]
                p0, p1 = pwm_raw[i - 1], pwm_raw[i]
                if v1 == v0:
                    duty = p0
                else:
                    duty = p0 + (target_v - v0) * (p1 - p0) / (v1 - v0)
                break

        lut.append(max(0, min(max_duty, int(round(duty)))))

    return lut


def interpolate_voltage(
    data: list[tuple[int, int]], pwm_pct: float
) -> float:
    """Interpolate voltage from measurement data for a given PWM%."""
    pwm_vals = [d[0] for d in data]
    v_vals = [d[1] for d in data]

    for i in range(len(data) - 1):
        if pwm_vals[i] <= pwm_pct <= pwm_vals[i + 1]:
            if pwm_vals[i + 1] == pwm_vals[i]:
                return float(v_vals[i])
            return v_vals[i] + (pwm_pct - pwm_vals[i]) * (
                v_vals[i + 1] - v_vals[i]
            ) / (pwm_vals[i + 1] - pwm_vals[i])

    return float(v_vals[-1])


def format_lut_cpp(lut: list[int], resolution_bits: int) -> str:
    """Format the LUT as a C++ constexpr array definition."""
    max_duty = (1 << resolution_bits) - 1
    dtype = "uint8_t" if resolution_bits <= 8 else "uint16_t"
    width = len(str(max_duty)) + 1

    lines = []
    lines.append(
        f"// user% (0-100) -> raw {resolution_bits}-bit LEDC duty "
        f"(0-{max_duty}), linearized"
    )
    lines.append(
        f"static constexpr {dtype} kFanLinearizationLut[101] = {{"
    )

    for i in range(0, 101, 11):
        chunk = lut[i : i + 11]
        values = ", ".join(str(v).rjust(width) for v in chunk)
        comment = f"/*{i:4d}% */"
        suffix = "," if i + 11 < 101 else ""
        lines.append(f"    {comment} {values}{suffix}")

    lines.append("};")
    return "\n".join(lines)


def print_verification(
    lut: list[int],
    data: list[tuple[int, int]],
    resolution_bits: int,
) -> None:
    """Print a verification table showing expected vs. target voltages."""
    max_duty = (1 << resolution_bits) - 1
    v_min = next(v for _, v in data if v > 0)
    v_max = data[-1][1]

    print("\nVerification (user% -> duty -> estimated voltage vs. target):")
    print("-" * 72)
    print(
        f"  {'User%':>6}  {'Duty':>6}/{max_duty}  {'PWM%':>7}  "
        f"{'~Voltage':>8}  {'Target':>8}  {'Error':>8}"
    )
    print("-" * 72)

    steps = list(range(0, 101, 5))
    max_err = 0.0

    for u in steps:
        d = lut[u]
        pct = d * 100.0 / max_duty
        v_est = interpolate_voltage(data, pct)
        target_v = v_min + u * (v_max - v_min) / 100.0 if u > 0 else 0.0
        err = v_est - target_v
        max_err = max(max_err, abs(err))
        print(
            f"  {u:5d}%  {d:6d}       {pct:6.2f}%  "
            f"{v_est:7.0f} mV  {target_v:7.0f} mV  {err:+7.0f} mV"
        )

    print("-" * 72)
    print(f"  Max absolute error: {max_err:.0f} mV")

    # Count distinct values in low range
    low_distinct = len(set(lut[1:21]))
    print(f"  Distinct duty values for user 1-20%: {low_distinct}")


def main() -> None:
    print("=" * 60)
    print("  Fan PWM Linearization LUT Generator")
    print("=" * 60)
    print(f"\n  Resolution:    {RESOLUTION_BITS}-bit "
          f"({(1 << RESOLUTION_BITS) - 1} steps)")
    print(f"  Measurements:  {len(MEASUREMENTS)} data points")
    print(f"  LUT size:      {101 * (2 if RESOLUTION_BITS > 8 else 1)} bytes")
    print()

    validate_measurements(MEASUREMENTS)

    lut = generate_lut(MEASUREMENTS, RESOLUTION_BITS)

    print("// clang-format off")
    print(format_lut_cpp(lut, RESOLUTION_BITS))
    print("// clang-format on")

    print_verification(lut, MEASUREMENTS, RESOLUTION_BITS)

    print(
        "\n>> Copy the array above into FanManager.cpp, replacing the "
        "existing kFanLinearizationLut.\n"
    )


if __name__ == "__main__":
    main()
