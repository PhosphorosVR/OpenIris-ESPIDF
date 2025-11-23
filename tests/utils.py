import time
import serial.tools.list_ports
from tools.openiris_device import OpenIrisDevice

OPENIRIS_DEVICE = None


class OpenIrisDeviceManager:
    def __init__(self):
        self._device: OpenIrisDevice | None = None
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

        if port and port != self._current_port:
            print(f"Port changed from {self._current_port} to {port}, reconnecting...")
            self._current_port = port

            if self._device:
                self._device.disconnect()
                self._device = None

            self._device = OpenIrisDevice(port, False, False)
            self._device.connect()
            time.sleep(int(config["SWITCH_MODE_REBOOT_TIME"]))

        return self._device


def has_command_failed(result) -> bool:
    return "error" in result or result["results"][0]["result"]["status"] != "success"


def get_current_ports() -> list[str]:
    return [port.name for port in serial.tools.list_ports.comports()]


def get_new_port(old_ports, new_ports) -> str:
    return list(set(new_ports) - set(old_ports))[0]
