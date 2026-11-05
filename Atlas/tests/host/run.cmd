@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /Istubs /I../../include /I../../../shared/include /I../../../shared/core/include scenarios.cpp test_globals.cpp profile_fixture.cpp ../../src/app_context.cpp ../../src/device_intents.cpp ../../src/sigil_input.cpp ../../src/sigil_menu.cpp ../../src/web_adapters.cpp ../../src/front_panel.cpp ../../src/touch_controls.cpp ../../src/audio_controller.cpp ../../src/led_renderer.cpp ../../src/web_api.cpp ../../src/web_session.cpp ../../src/web_profile_api.cpp ../../src/web_atlas_api.cpp ../../src/web_standalone_api.cpp ../../src/standalone_import.cpp ../../src/sigil_update_service.cpp ../../src/sigil_update_jobs.cpp ../../src/web_admin_api.cpp ../../src/profile_statistics.cpp ../../src/ota_manager.cpp ../../src/intent_dispatcher.cpp ../../src/nvs_blob_store.cpp ../../src/serial_log.cpp ../../src/match_archive.cpp ../../src/hosted_game.cpp ../../src/hosted_sigils.cpp ../../src/web_hosted_api.cpp ../../src/card_cache.cpp ../../src/plane_image.cpp ../../../shared/core/src/turnhub_json.cpp ../../../shared/core/src/turnhub_core.cpp /Fo:build/ /Fe:build/scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\scenarios.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /W4 /Istorage_stubs /Istubs /I../../include /I../../../shared/include /I../../../shared/core/include storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp ../../src/sd_blob_store.cpp ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp /Fo:build/ /Fe:build/storage_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\storage_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /W4 /Istorage_stubs /Istubs /I../../include /I../../../shared/include /I../../../shared/core/include profile_store_scenarios.cpp test_globals.cpp ../../src/profile_store.cpp ../../src/nvs_blob_store.cpp ../../src/profile_stats_storage.cpp ../../src/profile_policy.cpp /Fo:build/ /Fe:build/profile_store_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\profile_store_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /W4 /Istubs /I../../include /I../../../shared/include /I../../../shared/core/include ota_scenarios.cpp ../../src/sigil_update_jobs.cpp /Fo:build/ /Fe:build/ota_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\ota_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
set result=%errorlevel%
popd
exit /b %result%
