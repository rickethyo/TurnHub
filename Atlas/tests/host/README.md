# Atlas native regression scenarios

These tests compile the **actual** `main.cpp` handlers/adapters, `GameEngine`,
`Lobby`, and `IntentDispatcher`. Clock, radio, GPIO, web server, presentation, and
NVS boundaries are replaced by deterministic stubs. No firmware is flashed.

On Windows, run `Atlas\tests\host\run.cmd` from an x64 Native Tools Command
Prompt for Visual Studio (C++ workload required). Assertions must remain enabled.

With GCC/Clang on another host, from this directory:

```sh
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Istubs -I../../include scenarios.cpp \
    ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp \
    -o build/scenarios
./build/scenarios
```

Six scenario groups cover dispatcher ownership; lobby and lifecycle restrictions;
shared-seat win-response order and denial restoration; elimination versus
concession; PASS grace/cancellation/rollover and actor validation; optional NVS
absence versus injected type/handle/erase/commit errors.

`python audit_adapters.py` checks that current transport/input adapters do not
call canonical mutators or authoritative transition helpers directly.

This is application regression coverage, not proof of radio delivery, GPIO
debounce, flash persistence, buzzer/LED timing, browser authentication, or hardware
behavior. See `docs/engineering/ATLAS_INTENT_VERIFICATION.md` for the hardware gate.
