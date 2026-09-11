"""TurnHub hub audio controller using Raspberry Pi GPIO."""

from __future__ import annotations

import threading
import time

try:
    import RPi.GPIO as GPIO

except ImportError:
    GPIO = None


BUZZER_GPIO = 18


class AudioController:
    """
    Controls TurnHub's central passive buzzer.

    The buzzer is connected to:
        GPIO18 / physical pin 12 -> signal
        GND / physical pin 14    -> ground

    Audio runs in background threads so tones do not block
    the main TurnHub game loop.
    """

    def __init__(
        self,
        serial_controller=None,
    ) -> None:

        self.enabled = GPIO is not None

        self._lock = threading.Lock()
        self._shutdown = False

        if not self.enabled:

            print(
                "[AUDIO] RPi.GPIO unavailable. "
                "Audio disabled."
            )

            return

        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)

        GPIO.setup(
            BUZZER_GPIO,
            GPIO.OUT,
            initial=GPIO.LOW,
        )

        print(
            "[AUDIO] Hub buzzer ready on "
            f"GPIO{BUZZER_GPIO}."
        )

    # ========================================================
    # Basic Tone Generation
    # ========================================================

    def _play_tone(
        self,
        frequency: int,
        duration_ms: int,
    ) -> None:

        if (
            not self.enabled
            or self._shutdown
        ):
            return

        with self._lock:

            if self._shutdown:
                return

            pwm = GPIO.PWM(
                BUZZER_GPIO,
                frequency,
            )

            try:

                pwm.start(50)

                time.sleep(
                    duration_ms / 1000.0
                )

            finally:

                pwm.stop()

                GPIO.output(
                    BUZZER_GPIO,
                    GPIO.LOW,
                )

    def tone(
        self,
        frequency: int,
        duration_ms: int,
    ) -> None:

        if not self.enabled:
            return

        thread = threading.Thread(
            target=self._play_tone,
            args=(
                frequency,
                duration_ms,
            ),
            daemon=True,
        )

        thread.start()

    # ========================================================
    # TurnHub Sounds
    # ========================================================

    def countdown_tone(
        self,
        frequency: int = 700,
    ) -> None:

        self.tone(
            frequency,
            120,
        )

    def turn_pass(self) -> None:

        self.tone(
            1000,
            80,
        )

    def warning(self) -> None:

        self.tone(
            500,
            180,
        )

    def game_start(self) -> None:

        def sequence() -> None:

            self._play_tone(
                700,
                100,
            )

            time.sleep(0.05)

            self._play_tone(
                1000,
                100,
            )

            time.sleep(0.05)

            self._play_tone(
                1400,
                180,
            )

        threading.Thread(
            target=sequence,
            daemon=True,
        ).start()

    def game_over(self) -> None:

        def sequence() -> None:

            for frequency in (
                900,
                1200,
                1500,
            ):

                self._play_tone(
                    frequency,
                    150,
                )

                time.sleep(0.05)

        threading.Thread(
            target=sequence,
            daemon=True,
        ).start()

    # ========================================================
    # Shutdown
    # ========================================================

    def stop(self) -> None:

        self._shutdown = True

        if not self.enabled:
            return

        try:

            GPIO.output(
                BUZZER_GPIO,
                GPIO.LOW,
            )

            GPIO.cleanup(
                BUZZER_GPIO
            )

        except Exception:
            pass