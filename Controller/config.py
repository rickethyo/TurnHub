"""TurnHub configuration and constants."""

import os


# ============================================================
# Serial
# ============================================================

SERIAL_PORT = os.getenv("TURNHUB_SERIAL_PORT", "/dev/ttyUSB0")
SERIAL_BAUD = 9600
SERIAL_TIMEOUT = 0.05

ARDUINO_BOOT_WAIT = 2.0
RECONNECT_DELAY = 2.0


# ============================================================
# Physical Modules
# ============================================================

MODULE_IDS = [1, 2]


# ============================================================
# Application States
# ============================================================

STATE_LOBBY = "LOBBY"
STATE_STARTING = "STARTING"
STATE_RUNNING = "RUNNING"
STATE_PAUSED = "PAUSED"
STATE_GAME_OVER = "GAME_OVER"


# ============================================================
# Button Timing
# ============================================================

LONG_PRESS_SECONDS = 2.0
WIN_HOLD_SECONDS = 5.0


# ============================================================
# Warning Timer
# ============================================================

# The Arduino currently reports a quantized value between
# 5 seconds and 5 minutes.
#
# TurnHub uses that value only as the physical position of the
# potentiometer and remaps it into the actual warning settings.

POT_RAW_MIN_MS = 5_000
POT_RAW_MAX_MS = 300_000

# Lowest physical pot position disables the normal warning.
WARNING_OFF = 0

# All remaining pot positions select between 1 and 10 minutes.
WARNING_MIN_MINUTES = 1
WARNING_MAX_MINUTES = 10

# When warning is OFF, the active player's green LED comes on
# after 5 minutes, but TurnHub never enters red warning mode.
WARNING_OFF_GREEN_MS = 300_000

# Normal warning behavior.
WARNING_CAUTION_FRACTION = 0.75
WARNING_BLINK_INTERVAL = 0.50


# ============================================================
# Blue LED Behavior
# ============================================================

NEW_TURN_FLASH_SECONDS = 3.0
NEW_TURN_FLASH_INTERVAL = 0.20

BREATHE_MIN = 0
BREATHE_MAX = 255
BREATHE_PERIOD_SECONDS = 2.6


# ============================================================
# Lobby
# ============================================================

LOBBY_IDLE_LED_TIME = 0.50

LOBBY_FLASH_ON = 0.18
LOBBY_FLASH_OFF = 0.18
LOBBY_FLASH_GAP = 3.0


# ============================================================
# Start Countdown
# ============================================================

START_COUNTDOWN_SECONDS = 3.0
START_COUNTDOWN_FLASH_TIME = 0.25
START_COUNTDOWN_TONES = [1000, 1300, 1700]


# ============================================================
# Main Loop
# ============================================================

LOOP_SLEEP_SECONDS = 0.01