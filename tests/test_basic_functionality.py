from tests.utils import has_command_failed
import pytest


@pytest.mark.has_capability("wired")
def test_ping_wired(get_openiris_device, ensure_board_in_mode):
    device = get_openiris_device()
    device = ensure_board_in_mode("wifi", device)

    command_result = device.send_command("ping")
    assert not has_command_failed(command_result)
