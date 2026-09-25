@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /Istubs /I../../include /I../../../shared/include scenarios.cpp test_globals.cpp profile_fixture.cpp ../../src/app_context.cpp ../../src/gameplay_intents.cpp ../../src/table_intents.cpp ../../src/moderation_intent.cpp ../../src/sigil_input.cpp ../../src/sigil_menu.cpp ../../src/profile_picker.cpp ../../src/web_adapters.cpp ../../src/front_panel.cpp ../../src/touch_controls.cpp ../../src/harness_link.cpp ../../src/sigil_accessibility.cpp ../../src/audio_controller.cpp ../../src/led_renderer.cpp ../../src/controller_profiles.cpp ../../src/web_api.cpp ../../src/web_session.cpp ../../src/web_profile_api.cpp ../../src/web_game_api.cpp ../../src/web_admin_api.cpp ../../src/profile_statistics.cpp ../../src/stats_page.cpp ../../src/profile_login_page.cpp ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp ../../src/client_state.cpp ../../src/game_checkpoint.cpp ../../src/game_recovery.cpp ../../src/game_recovery_store.cpp ../../src/nvs_blob_store.cpp ../../src/serial_log.cpp /Fo:build/ /Fe:build/scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\scenarios.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /W4 /Istorage_stubs /Istubs /I../../include /I../../../shared/include storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp ../../src/sd_blob_store.cpp ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp /Fo:build/ /Fe:build/storage_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\storage_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /W4 /Istorage_stubs /Istubs /I../../include /I../../../shared/include profile_store_scenarios.cpp test_globals.cpp ../../src/profile_store.cpp ../../src/nvs_blob_store.cpp ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp /Fo:build/ /Fe:build/profile_store_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\profile_store_scenarios.exe
set result=%errorlevel%
popd
exit /b %result%
