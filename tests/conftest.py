from dataclasses import dataclass
import dotenv
import pytest
import time

from tests.utils import (
    OpenIrisDeviceManager,
    has_command_failed,
    get_current_ports,
    get_new_port,
)

board_capabilities = {
    "esp_eye": ["wired", "wireless"],
    "esp32AIThinker": ["wireless"],
    "esp32Cam": ["wireless"],
    "esp32M5Stack": ["wireless"],
    "facefocusvr_eye_L": ["wired", "measure_current"],
    "facefocusvr_eye_R": ["wired", "measure_current"],
    "facefocusvr_face": ["wired", "measure_current"],
    "project_babble": ["wireless", "wired"],
    "seed_studio": ["wireless", "wired"],
    "wrooms3": ["wireless", "wired"],
    "wrooms3QIO": ["wireless", "wired"],
    "wrover": ["wireless", "wired"],
}


def pytest_addoption(parser):
    parser.addoption("--board", action="store")
    parser.addoption(
        "--connection",
        action="store",
        help="Defines how to connect to the given board, wireless by ip/mdns or wired by com/cdc",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        "has_capability(caps): skip if the board does not have the capability",
    )
    config.addinivalue_line(
        "markers", "lacks_capability(caps): skip if the board DOES have the capability"
    )


@pytest.fixture(autouse=True)
def check_capability_marker(request, board_lacks_capability):
    """
    Autorun on each test, checks if the board we started with, has the required capability

    This lets us skip tests that are impossible to run on some boards - like for example:

    It's impossible to run wired tests on a wireless board
    It's impossible to run tests for measuring current on boards without this feature
    """
    if marker := request.node.get_closest_marker("has_capability"):
        if not len(marker.args):
            raise ValueError(
                "has_capability marker must be provided with a capability to check"
            )

        for capability in marker.args:
            if board_lacks_capability(capability):
                pytest.skip(f"Board does not have capability {capability}")


@pytest.fixture(autouse=True)
def check_lacks_capability_marker(request, board_lacks_capability):
    if lacks_capability_marker := request.node.get_closest_marker("lacks_capability"):
        if not len(lacks_capability_marker.args):
            raise ValueError(
                "lacks_capability marker must be provided with a capability to check"
            )

        for capability in lacks_capability_marker.args:
            if not board_lacks_capability(capability):
                pytest.skip(
                    "The board supports given capability: {required_capability}, skipping"
                )


@pytest.fixture(scope="session", autouse=True)
def board_name(request):
    board_name = request.config.getoption("--board")
    if not board_name:
        raise ValueError("No board defined")

    yield board_name


@pytest.fixture()
def board_lacks_capability(board_name):
    def func(capability: str):
        if board_name:
            if board_name not in board_capabilities:
                raise ValueError(f"Unknown board {board_name}")

            return capability not in board_capabilities[board_name]
        return True

    return func


@pytest.fixture(scope="session", autouse=True)
def board_connection(request):
    """
    Grabs the specified connection connection method, to be used ONLY for the initial connection. Everything after it HAS to be handled via Device Manager.
    Ports WILL change throughout the tests, device manager can keep track of that and reconnect the board as needed.
    """
    board_connection = request.config.getoption("--connection")

    if not board_connection:
        raise ValueError("No connection method defined")

    yield board_connection


@dataclass
class TestConfig:
    WIFI_SSID: str
    WIFI_BSSID: str
    WIFI_PASS: str
    SWITCH_MODE_REBOOT_TIME: int
    WIFI_CONNECTION_TIMEOUT: int
    INVALID_WIFI_CONNECTION_TIMEOUT: int

    def __init__(
        self,
        WIFI_SSID: str,
        WIFI_BSSID: str,
        WIFI_PASS: str,
        SWITCH_MODE_REBOOT_TIME: int,
        WIFI_CONNECTION_TIMEOUT: int,
        INVALID_WIFI_CONNECTION_TIMEOUT: int,
    ):
        self.WIFI_SSID = WIFI_SSID
        self.WIFI_BSSID = WIFI_BSSID
        self.WIFI_PASS = WIFI_PASS
        self.SWITCH_MODE_REBOOT_TIME = int(SWITCH_MODE_REBOOT_TIME)
        self.WIFI_CONNECTION_TIMEOUT = int(WIFI_CONNECTION_TIMEOUT)
        self.INVALID_WIFI_CONNECTION_TIMEOUT = int(INVALID_WIFI_CONNECTION_TIMEOUT)


@pytest.fixture(scope="session")
def config():
    config = TestConfig(**dotenv.dotenv_values())
    yield config


@pytest.fixture(scope="session")
def openiris_device_manager(board_connection, config):
    manager = OpenIrisDeviceManager()
    manager.get_device(board_connection, config)
    yield manager

    if manager._device:
        manager._device.disconnect()


@pytest.fixture()
def get_openiris_device(openiris_device_manager, config):
    def func(port: str | None = None, _config: dict | None = None):
        return openiris_device_manager.get_device(port, config or _config)

    return func


@pytest.fixture()
def ensure_board_in_mode(openiris_device_manager, config):
    """
    Given the OpenIrisDevice manager, grabs the current device and ensures it's in the required mode

    if not, sends the command to switch and attempts reconnection if necessary, returning the device back
    """
    supported_modes = ["wifi", "uvc"]

    def func(mode, device):
        if mode not in supported_modes:
            raise ValueError(f"{mode} is not a supported mode")

        command_result = device.send_command("get_device_mode")
        if has_command_failed(command_result):
            raise ValueError(f"Failed to get device mode, error: {command_result}")

        current_mode = command_result["results"][0]["result"]["data"]["mode"].lower()
        if mode == current_mode:
            return device

        old_ports = get_current_ports()
        command_result = device.send_command("switch_mode", {"mode": mode})
        if has_command_failed(command_result):
            raise ValueError("Failed to switch mode, rerun the tests")

        print("Rebooting the board after changing mode")
        device.send_command("restart_device")

        sleep_timeout = int(config.SWITCH_MODE_REBOOT_TIME)
        print(
            f"Sleeping for {sleep_timeout} seconds to allow the device to switch modes and boot up"
        )
        time.sleep(sleep_timeout)
        new_ports = get_current_ports()

        new_device = openiris_device_manager.get_device(
            get_new_port(old_ports, new_ports), config
        )
        return new_device

    return func


@pytest.fixture(scope="session", autouse=True)
def after_session_cleanup(openiris_device_manager, config):
    yield

    print("Cleanup: Resetting the config and restarting device")
    device = openiris_device_manager.get_device(config=config)
    device.send_command("reset_config", {"section": "all"})
    device.send_command("restart_device")
