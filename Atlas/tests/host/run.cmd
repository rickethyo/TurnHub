@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /Istubs /I../../include scenarios.cpp test_globals.cpp profile_fixture.cpp ../../src/controller_profiles.cpp ../../src/web_api.cpp ../../src/profile_statistics.cpp ../../src/stats_page.cpp ../../src/profile_login_page.cpp ../../src/game_engine.cpp ../../src/lobby.cpp ../../src/intent_dispatcher.cpp /Fo:build/ /Fe:build/scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\scenarios.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /W4 /Istorage_stubs /Istubs /I../../include storage_scenarios.cpp test_globals.cpp ../../src/nvs_blob_store.cpp ../../src/profile_stats_storage.cpp /Fo:build/ /Fe:build/storage_scenarios.exe
if errorlevel 1 (popd & exit /b 1)
build\storage_scenarios.exe
set result=%errorlevel%
popd
exit /b %result%
