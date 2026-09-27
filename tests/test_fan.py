"""Fan control tests.

These guard the three properties that the revision-aware fan rework must not
break, all of which are silent failures if they regress:

  * out-of-range percentages are still REJECTED, not clamped
  * get_fan_duty_cycle still carries fan_pwm_duty_cycle under that exact name
  * the fan drive revision is actually identified, not falling back

Only boards that carry a fan run these.
"""

import pytest

from tests.utils import has_command_failed


def data_of(command_result):
    return command_result["results"][0]["result"]["data"]


@pytest.fixture()
def fan_status(get_openiris_device):
    def read():
        device = get_openiris_device()
        result = device.send_command("get_fan_duty_cycle")
        assert not has_command_failed(result)
        return data_of(result)

    return read


@pytest.mark.has_capability("fan")
def test_fan_status_still_carries_the_original_field(fan_status):
    """The added fields are additive; existing consumers read this one."""
    status = fan_status()
    assert "fan_pwm_duty_cycle" in status
    assert 0 <= status["fan_pwm_duty_cycle"] <= 100


@pytest.mark.has_capability("fan")
def test_fan_status_reports_its_limits(fan_status):
    status = fan_status()
    for field in ("min_percent", "max_percent", "can_turn_off", "revision"):
        assert field in status, f"missing {field}"
    assert status["min_percent"] <= status["max_percent"]


@pytest.mark.has_capability("fan")
def test_fan_rejects_values_outside_the_range(get_openiris_device, fan_status):
    """Rejecting rather than clamping is the behaviour callers have always seen."""
    device = get_openiris_device()
    status = fan_status()

    for value in (status["max_percent"] + 1, status["min_percent"] - 1, 101, -1, 255):
        if 0 <= value <= 100 and status["min_percent"] <= value <= status["max_percent"]:
            continue  # inside the permitted range on this board, not a rejection case
        result = device.send_command("set_fan_duty_cycle", {"dutyCycle": value})
        assert has_command_failed(result), f"{value} should have been rejected"


@pytest.mark.has_capability("fan")
def test_fan_accepts_and_stores_its_own_bounds(get_openiris_device, fan_status):
    device = get_openiris_device()
    status = fan_status()
    original = status["fan_pwm_duty_cycle"]

    try:
        for value in (status["min_percent"], status["max_percent"]):
            result = device.send_command("set_fan_duty_cycle", {"dutyCycle": value})
            assert not has_command_failed(result), f"{value} should have been accepted"
            assert fan_status()["fan_pwm_duty_cycle"] == value
    finally:
        device.send_command("set_fan_duty_cycle", {"dutyCycle": original})


@pytest.mark.has_capability("fan")
def test_fan_drive_revision_is_identified(get_openiris_device):
    """A board that ends up on the fallback is running an inverted curve on Rev.5."""
    device = get_openiris_device()
    result = device.send_command("get_board_revision")
    assert not has_command_failed(result)

    data = data_of(result)
    assert data["revision"] in ("legacy", "rev5"), f"detection returned {data['revision']}"
    assert not data["fallback_active"], f"running the safe fallback: {data}"


@pytest.mark.has_capability("fan")
def test_fan_raw_duty_bounds(get_openiris_device, fan_status):
    """The bench command exists and refuses values the timer cannot hold."""
    device = get_openiris_device()
    original = fan_status()["fan_pwm_duty_cycle"]

    try:
        result = device.send_command("set_fan_raw_duty", {"raw": 0})
        assert not has_command_failed(result)
        max_raw = data_of(result)["max_raw"]

        assert has_command_failed(device.send_command("set_fan_raw_duty", {"raw": max_raw + 1}))
        assert has_command_failed(device.send_command("set_fan_raw_duty", {"raw": -1}))
    finally:
        device.send_command("set_fan_duty_cycle", {"dutyCycle": original})
