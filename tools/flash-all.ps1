<#
.SYNOPSIS
  One-button flash: pulls the latest repo, then flashes every attached TurnHub
  board with the firmware its MAC address says it should run.

.DESCRIPTION
  Boards are identified by MAC, never by COM port (ports change whenever the PC
  restarts). The MAC -> firmware table is read from
  Documentation/engineering/BOARD_INVENTORY.md, so that file stays the single
  source. Reading a MAC resets the board.

  - Unknown MACs are left alone and reported.
  - The test harness is never flashed (it must not receive Atlas or Sigil
    firmware); it is listed and skipped.
  - A dirty working tree stops the pull so local work is never overwritten.

.PARAMETER NoPull   Flash the working copy as it is, without fetching.
.PARAMETER DryRun   Identify boards and show the plan; build and flash nothing.
#>
param(
  [switch]$NoPull,
  [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$inventoryPath = Join-Path $root 'Documentation\engineering\BOARD_INVENTORY.md'

# Firmware column in BOARD_INVENTORY.md -> PlatformIO project folder and environment.
$targets = @{
  'atlas'      = @{ Dir = 'Atlas'; Env = 'atlas' }
  'sigil'      = @{ Dir = 'Sigil'; Env = 'sigil' }
  'sigil-oled' = @{ Dir = 'Sigil'; Env = 'sigil-oled' }
}

function Fail($message) { Write-Host "`nERROR: $message" -ForegroundColor Red; exit 1 }

function Find-Pio {
  $cmd = Get-Command pio -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  $penv = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
  if (Test-Path $penv) { return $penv }
  Fail 'PlatformIO (pio) was not found. Install it or add %USERPROFILE%\.platformio\penv\Scripts to PATH.'
}

function Read-Inventory {
  if (-not (Test-Path $inventoryPath)) { Fail "Missing $inventoryPath" }
  $rows = @()
  foreach ($line in Get-Content $inventoryPath) {
    if ($line -notmatch '^\|') { continue }
    $cells = $line.Trim('|').Split('|') | ForEach-Object { $_.Trim().Trim('`') }
    if ($cells.Count -lt 4) { continue }
    if ($cells[3] -notmatch '^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$') { continue }
    $rows += [pscustomobject]@{ Board = $cells[0]; Firmware = $cells[1].ToLower(); Mac = $cells[3].ToUpper() }
  }
  if (-not $rows.Count) { Fail 'No boards with MAC addresses were found in BOARD_INVENTORY.md.' }
  return $rows
}

function Get-SerialPorts {
  # USB serial adapters only (CH340 for Atlas, CP210x for Sigils and the harness).
  Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.Name -match '\((COM\d+)\)' -and $_.PNPDeviceID -like 'USB\*' } |
    ForEach-Object { [pscustomobject]@{ Port = $Matches[1]; Name = $_.Name } } |
    Sort-Object { [int]($_.Port -replace '\D') }
}

function Read-Mac($pio, $port) {
  $out = & $pio pkg exec -p tool-esptoolpy -- esptool.py --port $port read_mac 2>&1 | Out-String
  if ($out -match 'MAC:\s*([0-9A-Fa-f:]{17})') { return $Matches[1].ToUpper() }
  return $null
}

# --- 1. latest repo -------------------------------------------------------------
Set-Location $root
if (-not $NoPull) {
  Write-Host '== Updating the repo' -ForegroundColor Cyan
  if (git status --porcelain) {
    Fail 'You have uncommitted changes, so the repo was not updated. Commit or stash them, or run with -NoPull to flash this working copy.'
  }
  git pull --ff-only
  if ($LASTEXITCODE -ne 0) { Fail 'git pull failed (not a fast-forward, or no network). Nothing was flashed.' }
}
$branch = git branch --show-current
Write-Host ("Flashing from {0} at {1}" -f $branch, (git log --oneline -1))
if ($branch -ne 'master') { Write-Host "Note: this is not master." -ForegroundColor Yellow }

# --- 2. identify boards ---------------------------------------------------------
$pio = Find-Pio
$inventory = Read-Inventory
$ports = @(Get-SerialPorts)
if (-not $ports.Count) { Fail 'No USB serial boards are connected.' }

Write-Host "`n== Identifying $($ports.Count) connected board(s) by MAC" -ForegroundColor Cyan
$plan = @()
foreach ($p in $ports) {
  $mac = Read-Mac $pio $p.Port
  $match = if ($mac) { $inventory | Where-Object { $_.Mac -eq $mac } | Select-Object -First 1 }
  $action = 'skip'; $fw = ''; $why = ''
  if (-not $mac) { $why = 'could not read MAC (busy? close any serial monitor)' }
  elseif (-not $match) { $why = 'MAC is not in BOARD_INVENTORY.md' }
  elseif (-not $targets.ContainsKey($match.Firmware)) { $why = "$($match.Board): never flashed by this script" }
  else { $action = 'flash'; $fw = $match.Firmware }
  $plan += [pscustomobject]@{
    Port = $p.Port; Mac = $mac; Board = if ($match) { $match.Board } else { '?' }
    Action = $action; Env = $fw; Why = $why; Result = ''
  }
  $label = if ($action -eq 'flash') { "-> $fw" } else { "-> skipped ($why)" }
  Write-Host ("  {0,-6} {1,-18} {2,-12} {3}" -f $p.Port, $mac, $plan[-1].Board, $label)
}

$toFlash = @($plan | Where-Object Action -eq 'flash')
if (-not $toFlash.Count) { Fail 'No connected board matched a flashable firmware in BOARD_INVENTORY.md.' }
if ($DryRun) { Write-Host "`nDry run: nothing was built or flashed." -ForegroundColor Yellow; exit 0 }

# --- 3. build once per firmware, then flash each board --------------------------
foreach ($fw in ($toFlash.Env | Select-Object -Unique)) {
  $t = $targets[$fw]
  Write-Host "`n== Building $fw" -ForegroundColor Cyan
  Push-Location (Join-Path $root $t.Dir)
  & $pio run -e $t.Env
  $ok = $LASTEXITCODE -eq 0
  Pop-Location
  if (-not $ok) {
    foreach ($row in $toFlash | Where-Object Env -eq $fw) { $row.Result = 'BUILD FAILED' }
  }
}

foreach ($row in $toFlash) {
  if ($row.Result) { continue }
  $t = $targets[$row.Env]
  Write-Host "`n== Flashing $($row.Board) ($($row.Env)) on $($row.Port)" -ForegroundColor Cyan
  Push-Location (Join-Path $root $t.Dir)
  & $pio run -e $t.Env --target upload --upload-port $row.Port
  $row.Result = if ($LASTEXITCODE -eq 0) { 'flashed' } else { 'UPLOAD FAILED' }
  Pop-Location
}

# --- 4. summary -----------------------------------------------------------------
Write-Host "`n== Summary" -ForegroundColor Cyan
$bad = 0
foreach ($row in $plan) {
  $text = if ($row.Result) { $row.Result } else { "skipped ($($row.Why))" }
  $color = if ($row.Result -eq 'flashed') { 'Green' } elseif ($row.Result) { 'Red' } else { 'Yellow' }
  if ($row.Result -and $row.Result -ne 'flashed') { $bad++ }
  Write-Host ("  {0,-12} {1,-18} {2}" -f $row.Board, $row.Mac, $text) -ForegroundColor $color
}
exit $(if ($bad) { 1 } else { 0 })
