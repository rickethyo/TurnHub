"""TurnHub configuration and constants."""

import os


# ============================================================
# Serial
# ============================================================

SERIAL_BAUD = 9600
SERIAL_TIMEOUT = 0.05

MODULE_BOOT_WAIT = 2.0
RECONNECT_DELAY = 2.0


# ============================================================
# Physical Modules
# ============================================================

# Each player module is now an independent serial device.
#
# Module 0:
#   ESP32
#   White wiring
#
# Module 1:
#   Arduino
#   Red wiring
#
# Module identity is ultimately confirmed by the module's
# READY|<module_id> message rather than ttyUSB numbering.

MODULE_IDS = [0, 1]


# ============================================================
# Serial Device Paths
# ============================================================

# Stable /dev/serial/by-id paths are preferred over ttyUSB0,
# ttyUSB1, etc. because ttyUSB numbering can change after
# rebooting or reconnecting USB devices.
#
# Environment variables allow these paths to be overridden
# without modifying the source code.

MODULE_SERIAL_PORTS = {
    0: os.getenv(
        "TURNHUB_MODULE_0_PORT",
        "/dev/serial/by-id/"
        "usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_"
        "Controller_0001-if00-port0",
    ),

    1: os.getenv(
        "TURNHUB_MODULE_1_PORT",
        "/dev/serial/by-id/"
        "usb-FTDI_FT232R_USB_UART_A10K34RJ-if00-port0",
    ),
}


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

# These values mirror the module firmware.
#
# SHORT physical button:
#   PASS TURN
#
# TALL physical button:
#   ACTION
#
# ACTION supports SHORT, LONG, and WIN hold events.

LONG_PRESS_SECONDS = 2.0
WIN_HOLD_SECONDS = 5.0


# ============================================================
# Warning Timer
# ============================================================

# The physical warning potentiometer has been temporarily
# removed during the transition to independent player modules.
#
# For now, warning mode defaults to OFF.
#
# When warning is OFF, the active player's green LED comes on
# after five minutes, but TurnHub never enters red warning mode.

WARNING_OFF = 0
DEFAULT_WARNING_MS = WARNING_OFF

WARNING_OFF_GREEN_MS = 300_000

# Retained for future configurable warning behavior.
WARNING_MIN_MINUTES = 1
WARNING_MAX_MINUTES = 10

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

# Retained even though the physical buzzer is temporarily
# removed. This allows audio feedback to return later without
# changing countdown/game logic.
START_COUNTDOWN_TONES = [1000, 1300, 1700]


# ============================================================
# Main Loop
# ============================================================

LOOP_SLEEP_SECONDS = 0.01