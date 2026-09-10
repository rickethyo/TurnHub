"""Arduino serial connection and protocol handling."""

from __future__ import annotations

import time
from typing import Optional

import serial

from config import (
    ARDUINO_BOOT_WAIT,
    RECONNECT_DELAY,
    SERIAL_BAUD,
    SERIAL_PORT,
    SERIAL_TIMEOUT,
)


class SerialController:
    def __init__(self, port: str = SERIAL_PORT, baud: int = SERIAL_BAUD) -> None:
        self.port = port
        self.baud = baud
        self.serial: Optional[serial.Serial] = None
        self.last_connect_attempt = 0.0

    @property
    def connected(self) -> bool:
        return self.serial is not None and self.serial.is_open

    def connect(self) -> list[str]:
        """
        Open the Arduino connection and return startup lines.

        Do not reset the input buffer here.

        The Arduino sends READY and the current POT value after
        reboot, and TurnHub needs to consume those messages.
        """

        now = time.monotonic()

        if now - self.last_connect_attempt < RECONNECT_DELAY:
            return []

        self.last_connect_attempt = now

        try:
            self.serial = serial.Serial(
                self.port,
                self.baud,
                timeout=SERIAL_TIMEOUT,
                write_timeout=0.5,
            )

            # Most Uno boards reset when the serial connection opens.
            time.sleep(ARDUINO_BOOT_WAIT)

            startup_lines: list[str] = []
            deadline = time.monotonic() + 0.75

            while time.monotonic() < deadline:
                line = self._read_one()

                if line:
                    startup_lines.append(line)
                else:
                    time.sleep(0.01)

            print(f"[SERIAL] Connected to {self.port}")

            return startup_lines

        except (serial.SerialException, OSError) as exc:
            self.serial = None
            print(f"[SERIAL] Connection failed: {exc}")

            return []

    def disconnect(self) -> None:
        if self.serial is not None:
            try:
                self.serial.close()
            except Exception:
                pass

        self.serial = None

    def _read_one(self) -> Optional[str]:
        if not self.connected:
            return None

        try:
            raw = self.serial.readline()

            if not raw:
                return None

            return raw.decode(
                "utf-8",
                errors="replace",
            ).strip()

        except (serial.SerialException, OSError):
            self.disconnect()
            raise

    def read_lines(self) -> list[str]:
        if not self.connected:
            return []

        lines: list[str] = []

        try:
            while self.serial.in_waiting:
                line = self._read_one()

                if line:
                    lines.append(line)

        except (serial.SerialException, OSError):
            raise

        return lines

    def send(self, command: str) -> None:
        if not self.connected:
            return

        try:
            self.serial.write(
                (command + "\n").encode("utf-8")
            )

        except (serial.SerialException, OSError):
            self.disconnect()
            raise

    def off(self) -> None:
        self.send("OFF")

    def blue(self, module: int, brightness: int) -> None:
        brightness = max(
            0,
            min(255, int(brightness)),
        )

        self.send(
            f"BLUE|{module}|{brightness}"
        )

    def red(self, module: int, on: bool) -> None:
        self.send(
            f"RED|{module}|{1 if on else 0}"
        )

    def green(self, module: int, on: bool) -> None:
        self.send(
            f"GREEN|{module}|{1 if on else 0}"
        )

    def sound(
        self,
        frequency: int,
        duration_ms: int,
    ) -> None:

        self.send(
            f"SOUND|{int(frequency)}|{int(duration_ms)}"
        )