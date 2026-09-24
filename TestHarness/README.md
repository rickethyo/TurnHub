# TurnHub Hardware Test Harness

**Status:** Experimental scaffold. No radio transport or automated scenarios are implemented yet.

This target is intended to reuse the former Atlas ESP32 as a dedicated hardware-in-the-loop QA node. Its job is to behave like one or more TurnHub controllers against a real Atlas, exercise real transport paths, and report PASS/FAIL results without becoming another source of game state.

## Architectural boundary

- Atlas remains the sole authority for table/game state.
- The harness may emulate Sigil/controller inputs and inspect responses, but it must never decide game outcomes or mutate Atlas state directly.
- Shared ESP-NOW packet definitions come from `../shared/include/protocol.h`; do not copy them into this project.
- Test orchestration should drive the same public controller/transport boundaries used by real hardware.
- The harness must not depend on Atlas GPIO, display hardware, or the replacement Atlas board. That keeps it useful through the upcoming Atlas hardware migration.
- Pairing tests must exercise the real pairing rules rather than bypassing trust or directly editing pairing storage.

## Feature gate

1. **State owner:** Atlas. The harness owns only transient test-run state and observations.
2. **Intent/request:** Existing controller requests and protocol packets. Add no test-only gameplay semantics.
3. **Validator:** Atlas's normal transport adapters and Intent handlers.
4. **Persistence:** None planned for canonical state. Test results may eventually be streamed over serial or stored as disposable logs.
5. **Presentation:** Serial console first. A richer host-side runner can be added later if useful.
6. **Protocol change:** None for the scaffold. The harness consumes the shared protocol as a client.
7. **Third-party impact:** None currently.
8. **Accessibility impact:** None to gameplay. This is developer tooling only.

## Intended scenarios

The first useful scenario set should eventually cover:

- Pair a virtual Sigil through the real pairing window.
- Join/attach controllers using supported flows.
- Start a game through a normal Atlas controller path.
- Pass turns through multiple virtual Sigils.
- Exercise life/counter requests where supported.
- Disconnect and reconnect a virtual Sigil, then verify resynchronization.
- Send duplicate, delayed, invalid, or out-of-turn requests and verify safe rejection/recovery.
- End/reset a game through normal controller paths.
- Repeated/soak sequences after the basic smoke path is trustworthy.

Do not implement the full list until the replacement Atlas board work settles. The goal of this directory right now is to reserve a clean boundary for the harness, not to freeze details that may need rework.

## Planned serial surface

The command vocabulary is intentionally small and provisional:

```text
help
status
run smoke
run pairing
run standard-4p
run reconnect
run chaos
run soak
```

Only `help` and `status` are functional in the initial scaffold. `run ...` currently reports that scenarios are not implemented.

Future output should be machine-readable enough for a host script while remaining understandable at a serial monitor, for example:

```text
HARNESS|PASS|PAIR|sigil=1
HARNESS|PASS|TURN|from=1|to=2
HARNESS|FAIL|RECONNECT|reason=timeout
HARNESS|SUMMARY|passed=27|failed=1
```

## Build

From `TestHarness/`:

```text
pio run -e harness
pio run -e harness --target upload
pio device monitor
```

The initial target is `esp32dev`, matching the former Atlas development board. Pinout is intentionally irrelevant to the scaffold because the first implementation uses serial plus wireless transport only.
