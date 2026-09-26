param([string]$Compiler = "$env:USERPROFILE\.platformio\packages\toolchain-gccmingw32\bin\g++.exe")
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) {
    throw 'Pass -Compiler with a Windows GCC compiler path, or use run.cmd with MSVC.'
}
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$previousPath = $env:PATH
$env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    # MinGW needs GCC layout rules for the production packed radio packet.
    $flags = @('-std=c++14', '-Wall', '-Wextra', '-mno-ms-bitfields', '-static')
    & $Compiler @flags -Istubs -I../../include -I../../../shared/include scenarios.cpp test_globals.cpp profile_fixture.cpp ../../src/app_context.cpp ../../src/gameplay_intents.cpp ../../src/table_intents.cpp ../../src/moderation_intent.cpp ../../src/sigil_input.cpp ../../src/sigil_menu.cpp ../../src/profile_picker.cpp ../../src/web_adapters.cpp ../../src/front_panel.cpp ../../src/touch_controls.cpp ../../src/harness_link.cpp ../../src/sigil_accessibility.cpp ../../src/audio_controller.cpp ../../src/led_renderer.cpp ../../src/controller_profiles.cpp ../../src/web_api.cpp ../../src/web_session.cpp ../../src/web_profile_api.cpp ../../src/web_game_api.cpp ../../src/web_admin_api.cpp ../../src/profile_statistics.cpp ../../src/profile_stats_bridge.cpp ../../src/stats_page.cpp ../../src/profile_login_page.cpp ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp ../../src/client_state.cpp ../../src/game_checkpoint.cpp ../../src/game_recovery.cpp ../../src/game_recovery_store.cpp ../../src/nvs_blob_store.cpp ../../src/serial_log.cpp -o build/scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Gameplay test compilation failed.' }
    & .\build\scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Gameplay scenarios failed.' }
    & $Compiler @flags -Istorage_stubs -Istubs -I../../include -I../../../shared/include storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp ../../src/sd_blob_store.cpp ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp -o build/storage_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Storage test compilation failed.' }
    & .\build\storage_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Storage scenarios failed.' }
    & $Compiler @flags -Istorage_stubs -Istubs -I../../include -I../../../shared/include profile_store_scenarios.cpp test_globals.cpp ../../src/profile_store.cpp ../../src/nvs_blob_store.cpp ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp -o build/profile_store_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Profile store test compilation failed.' }
    & .\build\profile_store_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Profile store scenarios failed.' }
} finally {
    Pop-Location
    $env:PATH = $previousPath
}
