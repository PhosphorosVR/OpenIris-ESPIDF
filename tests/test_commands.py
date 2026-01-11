import time
from tests.utils import has_command_failed, DetectPortChange
import pytest


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


@pytest.mark.has_capability("wired", "wireless")
def test_changing_mode_to_wired(get_openiris_device, ensure_board_in_mode, config):
    device = get_openiris_device()

    # let's make sure we're in the wireless mode first, if we're going to try changing it
    device = ensure_board_in_mode("wifi", device)
    with DetectPortChange() as port_selector:
        command_result = device.send_command("switch_mode", {"mode": "uvc"})
        assert not has_command_failed(command_result)
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    # and since we've changed the ports
    device = get_openiris_device(port_selector.get_new_port())
    # initial read to flush the logs first
    device.send_command("get_device_mode")
    result = device.send_command("get_device_mode")
    assert not has_command_failed(result)


def test_changing_mode_same_mode(get_openiris_device):
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


def test_setting_mdns_name(get_openiris_device, ensure_board_in_mode, config):
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
    time.sleep(config.SWITCH_MODE_REBOOT_TIME)
    check_mdns_name(second_name)


@pytest.mark.parametrize("payload", [{"name": "awd"}, {}])
def test_setting_mdns_name_invalid_payload(get_openiris_device, payload):
    device = get_openiris_device()
    command_result = device.send_command("set_mdns", payload)
    assert has_command_failed(command_result)


@pytest.mark.has_capability("wired", "wireless")
# make this to be has_capabilities instead
def test_reboot_command(get_openiris_device, ensure_board_in_mode, config):
    device = ensure_board_in_mode("wifi", get_openiris_device())

    command_result = device.send_command("switch_mode", {"mode": "uvc"})
    assert not has_command_failed(command_result)

    # we're testing if rebooting actually triggers reboot
    # so to be 100% sure of that, we're changing the mode to UVC and looking for new port
    # which might be a little overkill kill and won't work on boards not supporting both modes
    with DetectPortChange() as port_selector:
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    assert port_selector.get_new_port()


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
def test_scan_networks(get_openiris_device, ensure_board_in_mode, config):
    # this test might run after some tests that affect the network on the device
    # which might prevent us from scanning and thus make the test fail, so we reset the config
    device = get_openiris_device()
    reset_command = device.send_command("reset_config", {"section": "all"})
    assert not has_command_failed(reset_command)

    with DetectPortChange() as port_selector:
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    device = ensure_board_in_mode(
        "wifi", get_openiris_device(port_selector.get_new_port())
    )
    command_result = device.send_command("scan_networks")
    assert not has_command_failed(command_result)
    assert len(command_result["results"][0]["result"]["data"]["networks"]) != 0


def test_get_config(get_openiris_device):
    device = get_openiris_device()
    command_result = device.send_command("get_config")
    assert not has_command_failed(command_result)


def test_reset_config_invalid_payload(get_openiris_device):
    # to test the config, we can do two things. Set the mdns, get the config, reset it, get it again and compare
    device = get_openiris_device()
    reset_command = device.send_command("reset_config")
    assert has_command_failed(reset_command)


def test_reset_config(get_openiris_device, config):
    device = get_openiris_device()
    command_result = device.send_command("set_mdns", {"hostname": "somedifferentname"})
    assert not has_command_failed(command_result)

    current_config = device.send_command("get_config")
    assert not has_command_failed(current_config)

    reset_command = device.send_command("reset_config", {"section": "all"})
    assert not has_command_failed(reset_command)

    # since the config was reset, but the data will still be held in memory, we need to reboot the device
    with DetectPortChange():
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    new_config = device.send_command("get_config")
    assert not has_command_failed(new_config)

    assert not new_config == current_config


@pytest.mark.has_capability("wireless")
def test_set_wifi_no_bssid_in_payload(
    get_openiris_device, ensure_board_in_mode, config
):
    device = get_openiris_device()
    reset_command = device.send_command("reset_config", {"section": "all"})
    assert not has_command_failed(reset_command)

    with DetectPortChange():
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    device = ensure_board_in_mode("wifi", device)
    params = {
        "name": "main",
        "ssid": config.WIFI_SSID,
        "password": config.WIFI_PASS,
        "channel": 0,
        "power": 0,
    }
    set_wifi_result = device.send_command("set_wifi", params)
    assert not has_command_failed(set_wifi_result)

    connect_wifi_result = device.send_command("connect_wifi")
    assert not -has_command_failed(connect_wifi_result)
    time.sleep(config.WIFI_CONNECTION_TIMEOUT)  # and let it try to for some time

    wifi_status_command = device.send_command("get_wifi_status")
    assert not has_command_failed(wifi_status_command)
    assert wifi_status_command["results"][0]["result"]["data"]["status"] == "connected"


@pytest.mark.has_capability("wireless")
def test_set_wifi_no_bssid(get_openiris_device, ensure_board_in_mode, config):
    device = get_openiris_device()
    reset_command = device.send_command("reset_config", {"section": "all"})
    assert not has_command_failed(reset_command)

    with DetectPortChange():
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    # now that the config is clear, let's try setting the wifi
    device = ensure_board_in_mode("wifi", device)
    params = {
        "name": "main",
        "ssid": config.WIFI_SSID,
        "bssid": "",
        "password": config.WIFI_PASS,
        "channel": 0,
        "power": 0,
    }
    set_wifi_result = device.send_command("set_wifi", params)
    assert not has_command_failed(set_wifi_result)

    # now, let's force connection and check if it worked
    connect_wifi_result = device.send_command("connect_wifi")
    assert not -has_command_failed(connect_wifi_result)
    time.sleep(config.WIFI_CONNECTION_TIMEOUT)  # and let it try to for some time

    wifi_status_command = device.send_command("get_wifi_status")
    assert not has_command_failed(wifi_status_command)
    assert wifi_status_command["results"][0]["result"]["data"]["status"] == "connected"


@pytest.mark.has_capability("wireless")
def test_set_wifi_correct_bssid(get_openiris_device, ensure_board_in_mode, config):
    device = get_openiris_device()
    reset_command = device.send_command("reset_config", {"section": "all"})
    assert not has_command_failed(reset_command)

    with DetectPortChange():
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    device = ensure_board_in_mode("wifi", device)
    params = {
        "name": "main",
        "ssid": config.WIFI_SSID,
        "bssid": config.WIFI_BSSID,
        "password": config.WIFI_PASS,
        "channel": 0,
        "power": 0,
    }
    set_wifi_result = device.send_command("set_wifi", params)
    assert not has_command_failed(set_wifi_result)

    connect_wifi_result = device.send_command("connect_wifi")
    assert not -has_command_failed(connect_wifi_result)
    time.sleep(config.WIFI_CONNECTION_TIMEOUT)

    wifi_status_command = device.send_command("get_wifi_status")
    assert not has_command_failed(wifi_status_command)
    assert wifi_status_command["results"][0]["result"]["data"]["status"] == "connected"


@pytest.mark.has_capability("wireless")
def test_set_wifi_nonexitant_bssid(get_openiris_device, ensure_board_in_mode, config):
    device = get_openiris_device()
    reset_command = device.send_command("reset_config", {"section": "all"})
    assert not has_command_failed(reset_command)

    with DetectPortChange():
        device.send_command("restart_device")
        time.sleep(config.SWITCH_MODE_REBOOT_TIME)

    device = ensure_board_in_mode("wifi", device)
    params = {
        "name": "main",
        "ssid": config.WIFI_SSID,
        "bssid": "99:99:99:99:99:99",  # a completely wrong BSSID, just to test that we fail to connect
        "password": config.WIFI_PASS,
        "channel": 0,
        "power": 0,
    }

    set_wifi_result = device.send_command("set_wifi", params)
    assert not has_command_failed(set_wifi_result)

    connect_wifi_result = device.send_command("connect_wifi")
    assert not -has_command_failed(connect_wifi_result)
    time.sleep(config.WIFI_CONNECTION_TIMEOUT)

    wifi_status_command = device.send_command("get_wifi_status")
    assert not has_command_failed(wifi_status_command)
    assert wifi_status_command["results"][0]["result"]["data"]["status"] == "error"


@pytest.mark.has_capability("wireless")
def test_set_wifi_invalid_network(get_openiris_device, ensure_board_in_mode, config):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    params = {
        "name": "main",
        "ssid": "PleaseDontBeARealNetwork",
        "bssid": "",
        "password": "AndThePasswordIsFake",
        "channel": 0,
        "power": 0,
    }
    set_wifi_result = device.send_command("set_wifi", params)
    # even if the network is fake, we should not fail to set it
    assert not has_command_failed(set_wifi_result)

    device.send_command("connect_wifi")

    time.sleep(
        config.INVALID_WIFI_CONNECTION_TIMEOUT
    )  # and let it try to for some time

    wifi_status_command = device.send_command("get_wifi_status")
    # the command should not fail as well, but we should get an error result
    assert not has_command_failed(wifi_status_command)
    assert wifi_status_command["results"][0]["result"]["data"]["status"] == "error"
    # and not to break other tests, clean up
    device.send_command("reset_config", {"section": "all"})
    device.send_command("restart_device")


@pytest.mark.has_capability("wireless")
@pytest.mark.parametrize(
    "payload",
    (
        {},
        {
            "ssid": "PleaseDontBeARealNetwork",
            "password": "AndThePasswordIsFake",
            "channel": 0,
            "power": 0,
        },
        {
            "name": "IaintGotNoNameAndIMustConnect",
            "password": "AndThePasswordIsFake",
            "channel": 0,
            "power": 0,
        },
    ),
)
def test_set_wifi_invalid_payload(ensure_board_in_mode, get_openiris_device, payload):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    set_wifi_result = device.send_command("set_wifi", payload)
    # even if the network is fake, we should not fail to set it
    assert has_command_failed(set_wifi_result)
    # and not to break other tests, clean up
    device.send_command("reset_config", {"section": "all"})
    device.send_command("restart_device")


def test_update_main_wifi_network(ensure_board_in_mode, get_openiris_device, config):
    # now that the config is clear, let's try setting the wifi
    device = ensure_board_in_mode("wifi", get_openiris_device())
    params1 = {
        "name": "main",
        "ssid": "Nada",
        "bssid": "",
        "password": "Nuuh",
        "channel": 0,
        "power": 0,
    }

    params2 = {
        **params1,
        "ssid": config.WIFI_SSID,
        "password": config.WIFI_PASS,
    }

    set_wifi_result = device.send_command("set_wifi", params1)
    assert not has_command_failed(set_wifi_result)

    set_wifi_result = device.send_command("set_wifi", params2)
    assert not has_command_failed(set_wifi_result)
    # and not to break other tests, clean up
    device.send_command("reset_config", {"section": "all"})
    device.send_command("restart_device")


def test_set_wifi_add_another_network(ensure_board_in_mode, get_openiris_device):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    params = {
        "name": "anotherNetwork",
        "ssid": "PleaseDontBeARealNetwork",
        "bssid": "",
        "password": "AndThePassowrdIsFake",
        "channel": 0,
        "power": 0,
    }
    set_wifi_result = device.send_command("set_wifi", params)
    assert not has_command_failed(set_wifi_result)
    # and not to break other tests, clean up
    device.send_command("reset_config", {"section": "all"})
    device.send_command("restart_device")


@pytest.mark.parametrize(
    "payload",
    (
        {
            "ssid": "testAP",
            "password": "12345678",
            "channel": 0,
        },
        {
            "ssid": "testAP",
            "channel": 0,
        },
        {
            "ssid": "testAP",
            "password": "12345678",
        },
        {
            "ssid": "testAP",
        },
        {
            "password": "12345678",
        },
    ),
)
def test_update_ap_wifi(ensure_board_in_mode, get_openiris_device, payload):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    result = device.send_command("update_ap_wifi", payload)
    assert not has_command_failed(result)
    # and not to break other tests, clean up
    device.send_command("reset_config", {"section": "all"})
    device.send_command("restart_device")


@pytest.mark.parametrize(
    "payload",
    (
        {},  # completely empty payload
        {
            "channel": 2
        },  # technically valid payload, but we're missing the network name,
        {
            "name": "IAMNOTTHERE",
            "channel": 2,
        },  # None-existent network
    ),
)
@pytest.mark.has_capability("wireless")
def test_update_wifi_command_fail(ensure_board_in_mode, get_openiris_device, payload):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    result = device.send_command("update_wifi", payload)
    assert has_command_failed(result)


@pytest.mark.parametrize(
    "payload",
    (
        {
            "name": "anotherNetwork",
            "ssid": "WEUPDATEDTHESSID",
            "password": "ACOMPLETELYDIFFERENTPASS",
            "channel": 1,
            "power": 2,
        },
        {
            "name": "anotherNetwork",
            "password": "ACOMPLETELYDIFFERENTPASS",
        },
        {
            "name": "anotherNetwork",
            "ssid": "WEUPDATEDTHESSID",
        },
        {
            "name": "anotherNetwork",
            "channel": 1,
        },
        {
            "name": "anotherNetwork",
            "power": 2,
        },
    ),
)
@pytest.mark.has_capability("wireless")
def test_update_wifi_command(ensure_board_in_mode, get_openiris_device, payload):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    params = {
        "name": "anotherNetwork",
        "ssid": "PleaseDontBeARealNetwork",
        "bssid": "",
        "password": "AndThePasswordIsFake",
        "channel": 0,
        "power": 0,
    }
    set_wifi_result = device.send_command("set_wifi", params)
    assert not has_command_failed(set_wifi_result)

    device = ensure_board_in_mode("wifi", get_openiris_device())
    result = device.send_command("update_wifi", payload)
    assert not has_command_failed(result)


@pytest.mark.parametrize(
    "payload",
    (
        {},
        {"name": ""},
    ),
)
@pytest.mark.has_capability("wireless")
def test_delete_network_fail(ensure_board_in_mode, get_openiris_device, payload):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    result = device.send_command("delete_network", payload)
    assert has_command_failed(result)


@pytest.mark.parametrize("payload", ({"name": "main"}, {"name": "NOTANETWORK"}))
@pytest.mark.has_capability("wireless")
def test_delete_network(ensure_board_in_mode, get_openiris_device, payload):
    device = ensure_board_in_mode("wifi", get_openiris_device())
    result = device.send_command("delete_network", payload)
    assert not has_command_failed(result)


@pytest.mark.parametrize(
    "payload",
    (
        {},
        {
            "vlip": 0,
            "hflip": 0,
            "framesize": 5,
            "quality": 7,
            "brightness": 2,
        },
        {
            "vlip": 0,
        },
        {
            "hflip": 0,
        },
        {
            "framesize": 5,
        },
        {
            "quality": 7,
        },
        {
            "brightness": 2,
        },
    ),
)
def test_update_camera(get_openiris_device, payload):
    device = get_openiris_device()
    result = device.send_command("update_camera", payload)
    assert not has_command_failed(result)
