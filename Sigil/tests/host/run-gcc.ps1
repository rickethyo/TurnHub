param([string]$Compiler = "$env:USERPROFILE\.platformio\packages\toolchain-gccmingw32\bin\g++.exe")
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler)) {
    throw 'Pass -Compiler with a Windows GCC compiler path.'
}
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$previousPath = $env:PATH
$env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force ../../.pio/host-tests | Out-Null
    & $Compiler -std=c++14 -Wall -Wextra -Werror -mno-ms-bitfields -static -DTURNHUB_DISPLAY_OLED=1 -Istubs -I../../include -I../../../shared/include oled_scenarios.cpp ../../src/oled_display.cpp ../../src/sigil_display.cpp ../../src/sigil_menu.cpp -o ../../.pio/host-tests/oled_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'OLED test compilation failed.' }
    & ..\..\.pio\host-tests\oled_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'OLED scenarios failed.' }
    & $Compiler -std=c++14 -Wall -Wextra -Werror -mno-ms-bitfields -static -Istubs -I../../include -I../../../shared/include led_scenarios.cpp ../../src/sigil_led.cpp -o ../../.pio/host-tests/led_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'LED test compilation failed.' }
    & ..\..\.pio\host-tests\led_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'LED scenarios failed.' }
    & $Compiler -std=c++14 -Wall -Wextra -Werror -mno-ms-bitfields -static -Istubs -I../../include -I../../../shared/include menu_scenarios.cpp ../../src/sigil_menu.cpp -o ../../.pio/host-tests/menu_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Menu test compilation failed.' }
    & ..\..\.pio\host-tests\menu_scenarios.exe
    if ($LASTEXITCODE -ne 0) { throw 'Menu scenarios failed.' }
} finally {
    Pop-Location
    $env:PATH = $previousPath
}
