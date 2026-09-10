"""TurnHub sound language."""

import time

from config import (
    POT_RAW_MAX_MS,
    POT_RAW_MIN_MS,
)

from serial_controller import SerialController


class AudioController:
    def __init__(
        self,
        serial_controller: SerialController,
    ) -> None:

        self.serial = serial_controller

    def tone(
        self,
        frequency: int,
        duration_ms: int,
        gap_ms: int = 0,
    ) -> None:

        self.serial.sound(
            frequency,
            duration_ms,
        )

        if duration_ms or gap_ms:
            time.sleep(
                (duration_ms + gap_ms) / 1000.0
            )

    def ready(self) -> None:
        self.tone(900, 70, 40)
        self.tone(1200, 90)

    def join(self) -> None:
        self.tone(1500, 75)

    def starter_selected(self) -> None:
        self.tone(1700, 60, 35)
        self.tone(2300, 80)

    def start_armed(self) -> None:
        self.tone(1100, 60, 35)
        self.tone(1500, 80)

    def countdown(
        self,
        second_index: int,
    ) -> None:

        tones = [
            1000,
            1300,
            1700,
        ]

        index = max(
            0,
            min(
                second_index,
                len(tones) - 1,
            ),
        )

        self.tone(
            tones[index],
            110,
        )

    def turn_pass(self) -> None:
        self.tone(2200, 90)

    def pause(self) -> None:
        self.tone(1800, 90, 120)
        self.tone(1800, 90)

    def resume(self) -> None:
        self.tone(1300, 60, 40)
        self.tone(1800, 80)

    def invalid(self) -> None:
        self.tone(500, 70, 35)
        self.tone(420, 90)

    def lobby_reset(self) -> None:
        self.tone(1800, 70, 30)
        self.tone(1250, 70, 30)
        self.tone(800, 100)

    def rematch(self) -> None:
        self.tone(1000, 60, 30)
        self.tone(1400, 60, 30)
        self.tone(1800, 90)

    def victory(self) -> None:

        notes = [
            (523, 110),
            (659, 110),
            (784, 110),
            (1047, 350),
        ]

        for index, (
            frequency,
            duration,
        ) in enumerate(notes):

            gap = (
                50
                if index < len(notes) - 1
                else 0
            )

            self.tone(
                frequency,
                duration,
                gap,
            )

    def pot_feedback(
        self,
        raw_pot_value: int,
    ) -> None:
        """
        Produce a short tone based on the physical pot position.

        Lowest position:
            ~700 Hz

        Highest position:
            ~2200 Hz
        """

        low_hz = 700
        high_hz = 2200

        ratio = (
            raw_pot_value - POT_RAW_MIN_MS
        ) / (
            POT_RAW_MAX_MS - POT_RAW_MIN_MS
        )

        ratio = max(
            0.0,
            min(1.0, ratio),
        )

        frequency = int(
            low_hz
            + ratio * (high_hz - low_hz)
        )

        self.tone(
            frequency,
            35,
        )