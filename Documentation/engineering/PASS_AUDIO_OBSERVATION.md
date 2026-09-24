# PASS Audio Observation

Observed during Atlas intent-foundation play testing on 2026-09-19:

- Some PASS requests do not produce an audible pass beep.
- The intermittent instant-PASS bug is tracked separately as GitHub issue #6.

## Current code-path finding

A successful PASS commit always reaches the audio request path in `handleCommitPassIntent` (`Atlas/src/gameplay_intents.cpp`; `main.cpp` at the time of this observation):

- `audio.sameModulePass(...)` when the next player shares the same Sigil module.
- `audio.turnPass(...)` otherwise.

Therefore the stale loop timestamp bug can change when a PASS commits, but it does not directly bypass the post-commit audio call.

## Audio delivery risks to verify

1. `AudioController::play()` can reject a sound when its 16-job queue is full and logs `ATLAS|AUDIO|QUEUE_FULL`.
2. `AudioController::sendTone()` calls `SigilBus::buzzer(...)` without checking or logging the returned success value.
3. `SigilBus::sendToMac()` can reject a transmit request if the ESP-NOW TX queue is full and logs `ATLAS|ESP_NOW|TX_QUEUE_FULL`.
4. Current pass audio targets the incoming player's Sigil, not necessarily the module on which PASS was pressed.

## Next validation

During repeated PASS testing, correlate any missing beep with:

- `ATLAS|GAME|PASS|COMMIT`
- `ATLAS|AUDIO|QUEUE_FULL`
- `ATLAS|ESP_NOW|TX_QUEUE_FULL`
- whether the pass was same-module or cross-module

If no queue-full condition is present, add explicit audio enqueue/send instrumentation before changing behavior.
