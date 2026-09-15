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
        self._pwm = None

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

        # Create exactly one PWM object for the lifetime of the
        # audio controller. RPi.GPIO does not allow multiple live
        # PWM objects to own the same GPIO channel.
        self._pwm = GPIO.PWM(
            BUZZER_GPIO,
            1000,
        )
        self._pwm.start(0)

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

            except Exception as exc:
                # A single audio failure should never kill the
                # background worker for the rest of the session.
                print(
                    f"[AUDIO] Playback error: {exc}"
                )

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

            if self._pwm is None:
                return

            self._pwm.ChangeFrequency(
                frequency
            )
            self._pwm.ChangeDutyCycle(50)

            try:
                time.sleep(
                    duration_ms / 1000.0
                )

            finally:
                self._pwm.ChangeDutyCycle(0)

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

    def shared_player_added(self) -> None:
        self._enqueue(
            (700, 65, 35),
            (1050, 65, 35),
            (1450, 120, 0),
        )

    def shared_player_removed(self) -> None:
        self._enqueue(
            (1450, 65, 35),
            (1000, 65, 35),
            (650, 120, 0),
        )

    def same_module_pass(self) -> None:
        self._enqueue(
            (950, 65, 40),
            (1250, 95, 0),
        )

    def web_controller_paired(self) -> None:
        self._enqueue(
            (900, 60, 35),
            (1350, 60, 35),
            (1750, 110, 0),
        )

    def web_controller_reassigned(self) -> None:
        self._enqueue(
            (1500, 70, 35),
            (950, 70, 35),
            (1500, 120, 0),
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
            if self._pwm is not None:
                self._pwm.ChangeDutyCycle(0)
                self._pwm.stop()
                self._pwm = None

            GPIO.output(
                BUZZER_GPIO,
                GPIO.LOW,
            )

            GPIO.cleanup(
                BUZZER_GPIO
            )

        except Exception:
            pass
