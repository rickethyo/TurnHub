"""TurnHub audio interface.

The prototype's physical buzzer has been temporarily removed.

This module intentionally preserves TurnHub's audio API so the
game/controller code does not need to know whether audio hardware
is currently installed.

When audio hardware returns later, implementation can be restored
here without changing the rest of the application.
"""


class AudioController:
    """Audio interface for TurnHub."""

    def __init__(self, serial_controller=None):
        # Retained for API compatibility and future hardware use.
        self.serial = serial_controller

        # Audio is intentionally disabled in the current prototype.
        self.enabled = False


    # ========================================================
    # Generic Tone
    # ========================================================

    def tone(self, frequency, duration_ms):
        """Play a tone if audio hardware is available."""

        if not self.enabled:
            return

        # Future implementation goes here.


    # ========================================================
    # Countdown Tone
    # ========================================================

    def countdown_tone(self, frequency):
        """Play one countdown tone."""

        if not self.enabled:
            return

        # Future implementation goes here.


    # ========================================================
    # Turn Pass
    # ========================================================

    def turn_pass(self):
        """Audio feedback for a turn pass."""

        if not self.enabled:
            return

        # Future implementation goes here.


    # ========================================================
    # Warning
    # ========================================================

    def warning(self):
        """Audio feedback for a turn warning."""

        if not self.enabled:
            return

        # Future implementation goes here.


    # ========================================================
    # Game Start
    # ========================================================

    def game_start(self):
        """Audio feedback when a game begins."""

        if not self.enabled:
            return

        # Future implementation goes here.


    # ========================================================
    # Game Over
    # ========================================================

    def game_over(self):
        """Audio feedback when a game ends."""

        if not self.enabled:
            return

        # Future implementation goes here.


    # ========================================================
    # Stop
    # ========================================================

    def stop(self):
        """Stop currently playing audio."""

        if not self.enabled:
            return

        # Future implementation goes here.