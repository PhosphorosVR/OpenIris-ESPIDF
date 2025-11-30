import time
from tests.utils import has_command_failed, DetectPortChange
import pytest


@pytest.mark.has_capability("wired")
def test_ping_wired(get_openiris_device, ensure_board_in_mode):
    device = get_openiris_device()
    device = ensure_board_in_mode("wifi", device)

    command_result = device.send_command("ping")
    assert not has_command_failed(command_result)


@pytest.mark.has_capability("wired")
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
    

@pytest.mark.has_capability("wireless")
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
