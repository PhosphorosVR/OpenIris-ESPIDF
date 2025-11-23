import time
import json
import serial


class OpenIrisDevice:
    def __init__(self, port: str, debug: bool, debug_commands: bool):
        self.port = port
        self.debug = debug
        self.debug_commands = debug_commands
        self.connection: serial.Serial | None = None
        self.connected = False

    def __enter__(self):
        self.connected = self.__connect()
        return self

    def __exit__(self, type, value, traceback):
        self.__disconnect()
        self.connected = False

    def connect(self):
        self.connected = self.__connect()

    def disconnect(self):
        self.__disconnect()
        self.connected = False

    def __connect(self) -> bool:
        print(f"📡 Connecting directly to {self.port}...")

        try:
            self.connection = serial.Serial(
                port=self.port, baudrate=115200, timeout=1, write_timeout=1
            )
            self.connection.dtr = False
            self.connection.rts = False
            print(f"✅ Connected to the device on {self.port}")
            return True
        except Exception as e:
            print(f"❌ Failed to connect to {self.port}: {e}")
            return False

    def __disconnect(self):
        if self.connection and self.connection.is_open:
            self.connection.close()
            print(f"🔌 Disconnected from {self.port}")

    def __check_if_response_is_complete(self, response) -> dict | None:
        try:
            return json.loads(response)
        except ValueError:
            return None

    def __read_response(self, timeout: int | None = None) -> dict | None:
        # we can try and retrieve the response now.
        # it should be more or less immediate, but some commands may take longer
        # so we gotta timeout
        timeout = timeout if timeout is not None else 15
        start_time = time.time()
        response_buffer = ""
        while time.time() - start_time < timeout:
            if self.connection.in_waiting:
                packet = self.connection.read_all().decode("utf-8", errors="ignore")
                if self.debug and packet.strip():
                    print(f"Received: {packet}")
                    print("-" * 10)
                    print(f"Current buffer: {response_buffer}")
                    print("-" * 10)

                # we can't rely on new lines to detect if we're done
                # nor can we assume that we're always gonna get valid json response
                # but we can assume that if we're to get a valid response, it's gonna be json
                # so we can start actually building the buffer only when
                # some part of the packet starts with "{", and start building from there
                # we can assume that no further data will be returned, so we can validate
                # right after receiving the last packet
                if (not response_buffer and "{" in packet) or response_buffer:
                    # assume we just started building the buffer and we've received the first packet

                    # alternative approach in case this doesn't work - we're always sending a valid json
                    # so we can start building the buffer from the first packet and keep trying to find the
                    # starting and ending brackets, extract that part and validate, if the message is complete, return
                    if not response_buffer:
                        starting_idx = packet.find("{")
                        response_buffer = packet[starting_idx:]
                    else:
                        response_buffer += packet

                    # and once we get something, we can validate if it's a valid json
                    if parsed_response := self.__check_if_response_is_complete(
                        response_buffer
                    ):
                        return parsed_response
            else:
                time.sleep(0.1)
        return None

    def is_connected(self) -> bool:
        return self.connected

    def send_command(
        self, command: str, params: dict | None = None, timeout: int | None = None
    ) -> dict:
        if not self.connection or not self.connection.is_open:
            return {"error": "Device Not Connected"}

        cmd_obj = {"commands": [{"command": command}]}
        if params:
            cmd_obj["commands"][0]["data"] = params

        # we're expecting the json string to end with a new line
        # to signify we've finished sending the command
        cmd_str = json.dumps(cmd_obj) + "\n"
        try:
            # clean it out first, just to be sure we're starting fresh
            self.connection.reset_input_buffer()
            if self.debug or self.debug_commands:
                print(f"Sending command: {cmd_str}")
            self.connection.write(cmd_str.encode())
            response = self.__read_response(timeout)

            if self.debug:
                print(f"Received response: {response}")

            return response or {"error": "Command timeout"}

        except Exception as e:
            return {"error": f"Communication error: {e}"}
