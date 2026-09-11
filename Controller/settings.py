"""TurnHub physical hub settings."""

from __future__ import annotations

import RPi.GPIO as GPIO


# Raspberry Pi BCM GPIO numbers
DIP_5_MIN_GPIO = 17    # Physical pin 11
DIP_10_MIN_GPIO = 27   # Physical pin 13


# Warning values
WARNING_DISABLED = 0
WARNING_5_MIN = 5 * 60 * 1000
WARNING_10_MIN = 10 * 60 * 1000


class SettingsController:
    """
    Reads TurnHub's physical configuration switches.

    DIP 1:
        GPIO17 / physical pin 11
        Enables 5-minute warning mode.

    DIP 2:
        GPIO27 / physical pin 13
        Enables 10-minute warning mode.

    Both switches use the Raspberry Pi's internal pull-ups.
    A switch is ON when its GPIO is connected to ground.
    """

    def __init__(self) -> None:

        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)

        GPIO.setup(
            DIP_5_MIN_GPIO,
            GPIO.IN,
            pull_up_down=GPIO.PUD_UP,
        )

        GPIO.setup(
            DIP_10_MIN_GPIO,
            GPIO.IN,
            pull_up_down=GPIO.PUD_UP,
        )

        self._last_mode = None

    def five_min_enabled(self) -> bool:
        return GPIO.input(DIP_5_MIN_GPIO) == GPIO.LOW

    def ten_min_enabled(self) -> bool:
        return GPIO.input(DIP_10_MIN_GPIO) == GPIO.LOW

    def warning_ms(self) -> int:

        five = self.five_min_enabled()
        ten = self.ten_min_enabled()

        # Both OFF
        if not five and not ten:
            return WARNING_DISABLED

        # 5-minute switch only
        if five and not ten:
            return WARNING_5_MIN

        # 10-minute switch only
        if ten and not five:
            return WARNING_10_MIN

        # Both ON is reserved.
        # Fail safely to timer disabled.
        return WARNING_DISABLED

    def description(self) -> str:

        five = self.five_min_enabled()
        ten = self.ten_min_enabled()

        if not five and not ten:
            return "DISABLED (green at 5 minutes)"

        if five and not ten:
            return "5 MINUTES"

        if ten and not five:
            return "10 MINUTES"

        return "RESERVED (both switches ON)"

    def changed(self) -> bool:

        current = (
            self.five_min_enabled(),
            self.ten_min_enabled(),
        )

        if current == self._last_mode:
            return False

        self._last_mode = current
        return True

    def cleanup(self) -> None:

        try:
            GPIO.cleanup(DIP_5_MIN_GPIO)
            GPIO.cleanup(DIP_10_MIN_GPIO)

        except Exception:
            pass