import time
import serial.tools.list_ports
from tools.openiris_device import OpenIrisDevice

OPENIRIS_DEVICE = None


class OpenIrisDeviceManager:
    def __init__(self):
        self._device: OpenIrisDevice | None = None
        self.stored_ports = []
        self._current_port: str | None = None

    def get_device(self, port: str | None = None, config=None) -> OpenIrisDevice:
        """
        Returns the current OpenIrisDevice connection helper

        if the port changed from the one given previously, it will attempt to reconnect
        if no device exists, we will create one and try to connect

        This helper is designed to be used within a session long fixture
        """
        if not port and not self._device:
            raise ValueError("No device connected yet, provide a port first")

        # I'm not sure if I like this approach
        # maybe I need to rethink this fixture
        current_ports = get_current_ports()
        new_port = get_new_port(self.stored_ports, current_ports)
        if new_port is not None:
            self.stored_ports = current_ports

        if not port:
            port = new_port

        if port and port != self._current_port:
            print(f"Port changed from {self._current_port} to {port}, reconnecting...")
            self._current_port = port

            if self._device:
                self._device.disconnect()
                self._device = None

            self._device = OpenIrisDevice(port, False, False)
            self._device.connect()
            time.sleep(config.SWITCH_MODE_REBOOT_TIME)

        return self._device


def has_command_failed(result) -> bool:
    return "error" in result or result["results"][0]["result"]["status"] != "success"


def get_current_ports() -> list[str]:
    return [port.name for port in serial.tools.list_ports.comports()]


def get_new_port(old_ports, new_ports) -> str:
    if ports_diff := list(set(new_ports) - set(old_ports)):
        return ports_diff[0]
    return None


class DetectPortChange:
    def __init__(self):
        self.old_ports = []
        self.new_ports = []

    def __enter__(self, *args, **kwargs):
        self.old_ports = get_current_ports()
        return self

    def __exit__(self, *args, **kwargs):
        self.new_ports = get_current_ports()

    def get_new_port(self):
        return get_new_port(self.old_ports, self.new_ports)
