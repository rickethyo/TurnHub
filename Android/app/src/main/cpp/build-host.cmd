@echo off
setlocal
set "out=%~1"
set "core=%~dp0..\..\..\..\..\core"
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vs=%%i"
if not defined vs exit /b 1
call "%vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%out%" mkdir "%out%"
set "sdk=%ANDROID_HOME%"
if not defined sdk set "sdk=%LOCALAPPDATA%\Android\Sdk"
rem JBR distributions omit headers; use the SDK's VM-neutral JNI header.
copy /y "%sdk%\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\include\jni.h" "%out%\jni.h" >nul
if errorlevel 1 exit /b 1
pushd "%out%"
cl /nologo /std:c++17 /EHsc /utf-8 /LD /MT /W4 /WX /I"%out%" /I"%core%\include" "%~dp0game_jni.cpp" "%core%\src\turnhub_core.cpp" "%core%\src\turnhub_json.cpp" /link /OUT:turnhub_game.dll
set "result=%errorlevel%"
popd
exit /b %result%
