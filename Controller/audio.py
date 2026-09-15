"""TurnHub hub audio controller using Raspberry Pi GPIO."""

from __future__ import annotations

import queue
import threading
import time

try:
    import RPi.GPIO as GPIO

except ImportError:
    GPIO = None


BUZZER_GPIO = 18


class AudioController:
    """Controls TurnHub's central passive buzzer."""

    def __init__(
        self,
        serial_controller=None,
    ) -> None:

        self.enabled = GPIO is not None
        self._shutdown = False
        self._queue: queue.Queue[
            tuple[tuple[int, int, int], ...]
        ] = queue.Queue()
        self._worker: threading.Thread | None = None

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

        self._worker = threading.Thread(
            target=self._audio_worker,
            daemon=True,
            name="TurnHub-Audio",
        )
        self._worker.start()

        print(
            "[AUDIO] Hub buzzer ready on "
            f"GPIO{BUZZER_GPIO}."
        )

    # ========================================================
    # Queue / Tone Generation
    # ========================================================

    def _audio_worker(self) -> None:
        """Play queued sound patterns one at a time."""

        while not self._shutdown:

            try:
                pattern = self._queue.get(
                    timeout=0.10
                )

            except queue.Empty:
                continue

            try:
                self._play_pattern(pattern)

            finally:
                self._queue.task_done()

    def _play_pattern(
        self,
        pattern: tuple[
            tuple[int, int, int], ...
        ],
    ) -> None:
        """
        Play one complete pattern without allowing another
        TurnHub sound to interleave with it.

        Each tuple is:
            (frequency_hz, duration_ms, gap_after_ms)
        """

        for frequency, duration_ms, gap_ms in pattern:

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

            if gap_ms > 0:
                time.sleep(
                    gap_ms / 1000.0
                )

    def _enqueue(
        self,
        *notes: tuple[int, int, int],
    ) -> None:

        if (
            not self.enabled
            or self._shutdown
            or not notes
        ):
            return

        self._queue.put(
            tuple(notes)
        )

    def tone(
        self,
        frequency: int,
        duration_ms: int,
    ) -> None:

        self._enqueue(
            (
                frequency,
                duration_ms,
                0,
            )
        )

    # ========================================================
    # TurnHub Sounds
    # ========================================================

    def player_joined(self) -> None:
        self._enqueue(
            (800, 60, 35),
            (1100, 90, 0),
        )

    def starter_selected(self) -> None:
        self._enqueue(
            (1100, 70, 35),
            (1500, 110, 0),
        )

    def random_starter(self) -> None:
        self._enqueue(
            (750, 55, 35),
            (950, 55, 35),
            (1200, 55, 35),
            (1550, 120, 0),
        )

    def start_armed(self) -> None:
        self._enqueue(
            (650, 120, 0),
        )

    def countdown_cancelled(self) -> None:
        self._enqueue(
            (650, 80, 35),
            (400, 130, 0),
        )

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

    def pause(self) -> None:
        self._enqueue(
            (1000, 90, 35),
            (650, 140, 0),
        )

    def resume(self) -> None:
        self._enqueue(
            (650, 90, 35),
            (1000, 140, 0),
        )

    def warning(self) -> None:
        self.tone(
            500,
            180,
        )

    def game_start(self) -> None:
        self._enqueue(
            (700, 100, 50),
            (1000, 100, 50),
            (1400, 180, 0),
        )

    def game_over(self) -> None:
        self._enqueue(
            (900, 150, 50),
            (1200, 150, 50),
            (1500, 150, 0),
        )

    # ========================================================
    # Shutdown
    # ========================================================

    def stop(self) -> None:

        self._shutdown = True

        if not self.enabled:
            return

        if (
            self._worker is not None
            and self._worker.is_alive()
        ):
            self._worker.join(
                timeout=0.50
            )

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
