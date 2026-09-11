"""Serial communication layer for TurnHub player modules."""

import threading
import time

import serial

import config


class SerialController:
    """
    Manages serial communication with independent TurnHub modules.

    Each physical player module has its own serial connection.

    Module 0:
        ESP32
        White wiring

    Module 1:
        Arduino
        Red wiring

    Modules identify themselves with:

        READY|<module_id>

    Incoming messages are forwarded to the application through
    the supplied callback.
    """

    def __init__(self, message_callback=None):
        self.message_callback = message_callback

        # module_id -> serial.Serial
        self.connections = {}

        # module_id -> reader thread
        self.reader_threads = {}

        # Protect serial writes and connection changes.
        self.lock = threading.Lock()

        self.running = False


    # ========================================================
    # Startup
    # ========================================================

    def start(self):
        """Open configured module ports and start reader threads."""

        self.running = True

        print("Starting TurnHub serial controller...")

        for module_id in config.MODULE_IDS:
            self._connect_module(module_id)


    # ========================================================
    # Shutdown
    # ========================================================

    def stop(self):
        """Stop all serial threads and close all connections."""

        print("Stopping TurnHub serial controller...")

        self.running = False

        with self.lock:
            connections = list(self.connections.items())
            self.connections.clear()

        for module_id, connection in connections:
            try:
                connection.close()
            except Exception:
                pass

        for thread in self.reader_threads.values():
            if thread.is_alive():
                thread.join(timeout=1.0)

        self.reader_threads.clear()


    # ========================================================
    # Module Connection
    # ========================================================

    def _connect_module(self, module_id):
        """Connect one configured TurnHub module."""

        port = config.MODULE_SERIAL_PORTS.get(module_id)

        if not port:
            print(
                f"No serial port configured for Module {module_id}"
            )
            return False

        try:
            print(
                f"Connecting Module {module_id}: {port}"
            )

            connection = serial.Serial(
                port=port,
                baudrate=config.SERIAL_BAUD,
                timeout=config.SERIAL_TIMEOUT,
            )

            # Opening the serial connection can reset Arduino/ESP32
            # development boards.
            time.sleep(config.MODULE_BOOT_WAIT)

            # Discard bootloader/reset noise before normal operation.
            connection.reset_input_buffer()

            with self.lock:
                self.connections[module_id] = connection

            thread = threading.Thread(
                target=self._reader_loop,
                args=(module_id,),
                daemon=True,
                name=f"TurnHub-Module-{module_id}",
            )

            self.reader_threads[module_id] = thread
            thread.start()

            print(
                f"Module {module_id} serial connection open"
            )

            return True

        except serial.SerialException as exc:
            print(
                f"Could not connect Module {module_id}: {exc}"
            )

            return False


    # ========================================================
    # Reader
    # ========================================================

    def _reader_loop(self, expected_module_id):
        """
        Continuously read messages from one physical module.
        """

        while self.running:

            with self.lock:
                connection = self.connections.get(
                    expected_module_id
                )

            if connection is None:
                break

            try:
                raw = connection.readline()

                if not raw:
                    continue

                try:
                    message = raw.decode(
                        "utf-8",
                        errors="replace",
                    ).strip()

                except Exception:
                    continue

                if not message:
                    continue

                print(
                    f"[Module {expected_module_id}] "
                    f"{message}"
                )

                self._handle_incoming(
                    expected_module_id,
                    message,
                )

            except serial.SerialException as exc:

                print(
                    f"Module {expected_module_id} "
                    f"disconnected: {exc}"
                )

                self._remove_connection(
                    expected_module_id
                )

                break

            except OSError as exc:

                print(
                    f"Module {expected_module_id} "
                    f"I/O error: {exc}"
                )

                self._remove_connection(
                    expected_module_id
                )

                break


    # ========================================================
    # Incoming Validation
    # ========================================================

    def _handle_incoming(
        self,
        expected_module_id,
        message,
    ):
        """
        Validate module identity where possible, then forward the
        message to TurnHub.
        """

        # ----------------------------------------------------
        # READY|module
        # ----------------------------------------------------

        if message.startswith("READY|"):

            parts = message.split("|")

            if len(parts) != 2:
                print(
                    f"Malformed READY from Module "
                    f"{expected_module_id}: {message}"
                )
                return

            try:
                reported_module_id = int(parts[1])

            except ValueError:
                print(
                    f"Invalid READY from Module "
                    f"{expected_module_id}: {message}"
                )
                return

            if reported_module_id != expected_module_id:

                print(
                    "MODULE ID MISMATCH: "
                    f"expected {expected_module_id}, "
                    f"device reported "
                    f"{reported_module_id}"
                )

                return


        # ----------------------------------------------------
        # PASS|module
        # ----------------------------------------------------

        elif message.startswith("PASS|"):

            parts = message.split("|")

            if len(parts) != 2:
                return

            try:
                reported_module_id = int(parts[1])

            except ValueError:
                return

            if reported_module_id != expected_module_id:

                print(
                    "PASS module mismatch: "
                    f"port belongs to {expected_module_id}, "
                    f"message says {reported_module_id}"
                )

                return


        # ----------------------------------------------------
        # ACTION|module|event
        # ----------------------------------------------------

        elif message.startswith("ACTION|"):

            parts = message.split("|")

            if len(parts) != 3:
                return

            try:
                reported_module_id = int(parts[1])

            except ValueError:
                return

            if reported_module_id != expected_module_id:

                print(
                    "ACTION module mismatch: "
                    f"port belongs to {expected_module_id}, "
                    f"message says {reported_module_id}"
                )

                return


        # Forward validated message to the application.

        if self.message_callback is not None:

            try:
                self.message_callback(message)

            except Exception as exc:
                print(
                    f"Serial message callback error: {exc}"
                )


    # ========================================================
    # Remove Connection
    # ========================================================

    def _remove_connection(self, module_id):

        with self.lock:

            connection = self.connections.pop(
                module_id,
                None,
            )

        if connection is not None:

            try:
                connection.close()

            except Exception:
                pass


    # ========================================================
    # Send Raw Command
    # ========================================================

    def send(self, module_id, command):
        """
        Send a command to one specific module.

        Example:

            send(0, "GREEN|0|1")
        """

        with self.lock:
            connection = self.connections.get(module_id)

        if connection is None:
            return False

        try:

            data = (
                command.rstrip("\r\n") + "\n"
            ).encode("utf-8")

            connection.write(data)

            return True

        except (
            serial.SerialException,
            OSError,
        ) as exc:

            print(
                f"Write failed for Module "
                f"{module_id}: {exc}"
            )

            self._remove_connection(module_id)

            return False


    # ========================================================
    # LED Commands
    # ========================================================

    def set_blue(self, module_id, brightness):

        brightness = max(
            0,
            min(255, int(brightness)),
        )

        self.send(
            module_id,
            f"BLUE|{module_id}|{brightness}",
        )


    def set_red(self, module_id, state):

        self.send(
            module_id,
            f"RED|{module_id}|"
            f"{1 if state else 0}",
        )


    def set_green(self, module_id, state):

        self.send(
            module_id,
            f"GREEN|{module_id}|"
            f"{1 if state else 0}",
        )


    def all_off(self):
        """Turn off every connected module."""

        for module_id in config.MODULE_IDS:
            self.send(
                module_id,
                "OFF",
            )


    # ========================================================
    # Connection Information
    # ========================================================

    def is_connected(self, module_id):
        """Return True if a module currently has an open port."""

        with self.lock:

            connection = self.connections.get(
                module_id
            )

            return (
                connection is not None
                and connection.is_open
            )


    def connected_modules(self):
        """Return a sorted list of connected module IDs."""

        with self.lock:

            return sorted(
                module_id
                for module_id, connection
                in self.connections.items()
                if connection.is_open
            )