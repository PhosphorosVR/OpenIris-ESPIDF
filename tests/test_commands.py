import time
from tests.utils import has_command_failed, DetectPortChange
import pytest

## TODO
# {"ping", CommandType::PING}, - tested
# {"set_wifi", CommandType::SET_WIFI},
# {"update_wifi", CommandType::UPDATE_WIFI},
# {"delete_network", CommandType::DELETE_NETWORK},
# {"update_ap_wifi", CommandType::UPDATE_AP_WIFI},
# {"set_mdns", CommandType::SET_MDNS}, - tested
# {"get_mdns_name", CommandType::GET_MDNS_NAME}, - tested
# {"update_camera", CommandType::UPDATE_CAMERA},
# {"get_config", CommandType::GET_CONFIG}, - tested
# {"reset_config", CommandType::RESET_CONFIG}, -tested
# {"restart_device", CommandType::RESTART_DEVICE} - tested,
# {"scan_networks", CommandType::SCAN_NETWORKS}, - tested
# {"get_wifi_status", CommandType::GET_WIFI_STATUS}, - tested
# {"switch_mode", CommandType::SWITCH_MODE}, - tested
# {"get_device_mode", CommandType::GET_DEVICE_MODE}, - tested
# {"set_led_duty_cycle", CommandType::SET_LED_DUTY_CYCLE}, - tested
# {"get_led_duty_cycle", CommandType::GET_LED_DUTY_CYCLE}, - tested
# {"get_serial", CommandType::GET_SERIAL}, - tested
# {"get_led_current", CommandType::GET_LED_CURRENT}, - tested
# {"get_who_am_i", CommandType::GET_WHO_AM_I}, - tested


def test_sending_invalid_command(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("some_invalid_command")
    assert has_command_failed(command_result)


def test_sending_invalid_command_with_payload(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("some_invalid_command", {"param": "invalid"})
    assert has_command_failed(command_result)


def test_ping_wired(get_openiris_device):
    device = get_openiris_device()

    command_result = device.send_command("ping")
    assert not has_command_failed(command_result)


@pytest.mark.has_capability("wired")
@pytest.mark.has_capability("wireless")
def test_changing_mode_to_wired(get_openiris_device, ensure_board_in_mode):
    device = get_openiris_device()

    # let's make sure we're in the wireless mode first, if we're going to try changing it
    device = ensure_board_in_mode("wifi", device)
    command_result = device.send_command("switch_mode", {"mode": "uvc"})
    assert not has_command_failed(command_result)

    # to avoid any issues, let's restart the board
    with DetectPortChange() as port_selector:
        device.send_command("restart_device")
        time.sleep(3)

    # and since we've changed the ports
    device = get_openiris_device(port_selector.get_new_port())
    # initial read to flush the logs first
    device.send_command("get_device_mode")
    result = device.send_command("get_device_mode")
    assert not has_command_failed(result)


def test_changing_mode_same_mode(get_openiris_device, ensure_board_in_mode):
    device = get_openiris_device()
    result = device.send_command("get_device_mode")
    current_mode = result["results"][0]["result"]["data"]["mode"].lower()
    command_result = device.send_command("switch_mode", {"mode": current_mode})
    assert not has_command_failed(command_result)

    result = device.send_command("get_device_mode")
    assert not has_command_failed(result)
    assert result["results"][0]["result"]["data"]["mode"].lower() == current_mode


def test_changing_mode_invalid_mode(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("switch_mode", {"mode": "NOT SUPPORTED"})
    assert has_command_failed(command_result)


def test_setting_mdns_name(get_openiris_device, ensure_board_in_mode):
    def check_mdns_name(name: str):
        command_result = device.send_command("get_mdns_name")
        assert not has_command_failed(command_result)
        assert command_result["results"][0]["result"]["data"]["hostname"] == name

    device = get_openiris_device()
    device = ensure_board_in_mode("wifi", device)
    first_name = "testname1"
    second_name = "testname2"
    # try setting the test mdns name first, just so we know the commands pass
    command_result = device.send_command("set_mdns", {"hostname": first_name})
    assert not has_command_failed(command_result)

    check_mdns_name(first_name)

    command_result = device.send_command("set_mdns", {"hostname": second_name})
    assert not has_command_failed(command_result)

    device.send_command("restart_device")
    # let the board boot, wait till it connects
    time.sleep(3)
    check_mdns_name(second_name)


@pytest.mark.parametrize("payload", [{"name": "awd"}, {}])
def test_setting_mdns_name_invalid_payload(get_openiris_device, payload):
    device = get_openiris_device()
    command_result = device.send_command("set_mdns", payload)
    assert has_command_failed(command_result)


@pytest.mark.has_capability("wired")
@pytest.mark.has_capability("wireless")
def test_reboot_command(get_openiris_device, ensure_board_in_mode):
    device = ensure_board_in_mode("wifi", get_openiris_device())

    command_result = device.send_command("switch_mode", {"mode": "uvc"})
    assert not has_command_failed(command_result)

    # we're testing if rebooting actually triggers reboot
    # so to be 100% sure of that, we're changing the mode to UVC and looking for new port
    # which might be a little overkill kill and won't work on boards not supporting both modes
    with DetectPortChange() as port_selector:
        device.send_command("restart_device")
        time.sleep(3)

    assert len(port_selector.get_new_port())


def test_get_serial(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("get_serial")
    assert not has_command_failed(command_result)
    # test for response integrity as well to uphold the contract
    assert "mac" in command_result["results"][0]["result"]["data"]
    assert "serial" in command_result["results"][0]["result"]["data"]


def test_get_who_am_i(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("get_who_am_i")
    assert not has_command_failed(command_result)
    # test for response integrity as well to uphold the contract
    assert "version" in command_result["results"][0]["result"]["data"]
    assert "who_am_i" in command_result["results"][0]["result"]["data"]


@pytest.mark.has_capability("measure_current")
def test_get_led_current_supported(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("get_led_current")
    assert not has_command_failed(command_result)
    assert "led_current_ma" in command_result["results"][0]["result"]["data"]


@pytest.mark.lacks_capability("measure_current")
def test_get_led_current_unsupported(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("get_led_current")
    assert has_command_failed(command_result)


def test_get_led_duty_cycle(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("get_led_duty_cycle")
    assert not has_command_failed(command_result)
    assert (
        "led_external_pwm_duty_cycle" in command_result["results"][0]["result"]["data"]
    )


def test_set_led_duty_cycle(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("set_led_duty_cycle", {"dutyCycle": 0})
    assert not has_command_failed(command_result)

    command_result = device.send_command("get_led_duty_cycle")
    assert not has_command_failed(command_result)
    assert (
        command_result["results"][0]["result"]["data"]["led_external_pwm_duty_cycle"]
        == 0
    )

    command_result = device.send_command("set_led_duty_cycle", {"dutyCycle": 100})
    assert not has_command_failed(command_result)

    command_result = device.send_command("get_led_duty_cycle")
    assert not has_command_failed(command_result)
    assert (
        command_result["results"][0]["result"]["data"]["led_external_pwm_duty_cycle"]
        == 100
    )


@pytest.mark.parametrize(
    "payload",
    [
        {},
        {"dutyCycle": -1},
        {"dutyCycle": 1.5},
        {"dutyCycle": 150},
        {"awd": 21},
        {"dutyCycle": "21"},
    ],
)
def test_set_led_duty_cycle_invalid_payload(get_openiris_device, payload):
    device = get_openiris_device()
    command_result = device.send_command("set_led_duty_cycle", payload)
    assert has_command_failed(command_result)


@pytest.mark.has_capability("wireless")
def test_check_wifi_status(get_openiris_device, ensure_board_in_mode):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    command_result = device.send_command("get_wifi_status")
    assert not has_command_failed(command_result)


@pytest.mark.has_capability("wireless")
def test_scan_networks(get_openiris_device, ensure_board_in_mode):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    command_result = device.send_command("scan_networks")
    assert not has_command_failed(command_result)
    assert len(command_result["results"][0]["result"]["data"]["networks"]) != 0


def test_get_config(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("get_config")
    assert not has_command_failed(command_result)


def test_reset_config(get_openiris_device):
    # to test the config, we can do two things. Set the mdns, get the config, reset it, get it again and compare
    device = get_openiris_device()
    command_result = device.send_command("set_mdns", {"hostname": "somedifferentname"})
    assert not has_command_failed(command_result)

    current_config = device.send_command("get_config")
    assert not has_command_failed(current_config)

    reset_command = device.send_command("reset_config")
    assert not has_command_failed(reset_command)

    new_config = device.send_command("get_config")
    assert not has_command_failed(new_config)

    assert not new_config == current_config
