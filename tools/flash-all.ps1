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
.PARAMETER Signed   Flash the signed GitHub release over USB instead of building: downloads
                    the release's .thfw packages, checks size, SHA-256 and signature, strips
                    the 128-byte signed header and writes that exact image. No pull, no build.
.PARAMETER Release  With -Signed: a tag such as v0.9.2. Default: the latest release.
.PARAMETER Sign     Build this working copy, sign it with the local key (Private\TurnHub-keys,
                    via sign-local.ps1), verify it, and flash the signed package's payload over
                    USB. The same package can be installed by OTA. Pulls first when the working
                    copy is clean; with uncommitted changes it signs them as they are, unpulled.
                    flash-all.cmd runs this mode; flash-all-unsigned.cmd runs the plain build.
.PARAMETER Key      With -Sign: path to the PEM key. Default: the one .pem in Private\TurnHub-keys.

  -Signed and -Sign write only the app image (and blank the OTA-selection sector so the
  board boots it); settings, profiles and pairings in NVS are kept. Downgrades work over USB.
#>
param(
  [switch]$NoPull,
  [switch]$DryRun,
  [switch]$Signed,
  [string]$Release = 'latest',
  [switch]$Sign,
  [string]$Key
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
if ($Signed -and $Sign) { Fail 'Use either -Signed (the release) or -Sign (your local build), not both.' }
if ($Signed) {
  Write-Host "Signed mode: flashing the $Release GitHub release (no pull, no build)." -ForegroundColor Cyan
} else {
  if ($Sign) { Write-Host 'Local signing mode: building and signing with your local key.' -ForegroundColor Cyan }
  if ($Sign -and -not $NoPull -and (git status --porcelain)) {
    # Uncommitted work is what you want signed: keep it and skip the pull.
    Write-Host 'Uncommitted changes: signing this working copy without pulling.' -ForegroundColor Yellow
  } elseif (-not $NoPull) {
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
}

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
if ($DryRun -and -not ($Signed -or $Sign)) { Write-Host "`nDry run: nothing was built or flashed." -ForegroundColor Yellow; exit 0 }

# --- 2b. signed images over USB (-Signed release, -Sign local key) ------------------
if ($Signed -or $Sign) {
  $products = @{ 'atlas' = 'atlas'; 'sigil' = 'sigil-eink'; 'sigil-oled' = 'sigil-oled' }
  $python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
  if (-not (Test-Path $python)) { $python = 'python' }
  & $python -c 'import cryptography' 2>$null
  if ($LASTEXITCODE -ne 0) { Fail "python 'cryptography' is missing: python -m pip install cryptography==46.0.7" }
  $thfw = Join-Path $root 'tools\firmware\thfw.py'
  $work = Join-Path $env:TEMP 'turnhub-signed'
  New-Item -ItemType Directory -Force $work | Out-Null
  $packages = @{}   # firmware column -> .thfw path
  $versions = @{}

  if ($Signed) {
    $repoUrl = 'https://github.com/rickethyo/TurnHub/releases'
    $base = if ($Release -eq 'latest') { "$repoUrl/latest/download" } else { "$repoUrl/download/$Release" }
    Write-Host "`n== Downloading the release feed" -ForegroundColor Cyan
    $feed = Invoke-RestMethod "$base/turnhub-firmware.json"
    Write-Host "Release $($feed.release)"
    $label = "signed release $($feed.release)"
    foreach ($fw in ($toFlash.Env | Select-Object -Unique)) {
      $pkg = $feed.packages | Where-Object product -eq $products[$fw]
      if (-not $pkg) { Fail "The release has no $($products[$fw]) package." }
      $file = Join-Path $work $pkg.file
      Write-Host "Downloading $($pkg.file) ($($pkg.version))"
      Invoke-WebRequest "$base/$($pkg.file)" -OutFile $file
      if ((Get-Item $file).Length -ne $pkg.size) { Fail "$($pkg.file) is not the size the release says." }
      if ((Get-FileHash $file -Algorithm SHA256).Hash.ToLower() -ne $pkg.sha256) { Fail "$($pkg.file) does not match the release's SHA-256." }
      $packages[$fw] = $file
    }
  } else {
    $label = 'locally signed build'
    $signDir = Join-Path (Join-Path (Split-Path -Parent $root) 'Private') ("TurnHub-builds\local-{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), (git rev-parse --short=12 HEAD))
    $signArgs = @('-Products', (($toFlash.Env | Select-Object -Unique | ForEach-Object { $products[$_] }) -join ','), '-OutDir', $signDir)
    if ($Key) { $signArgs += @('-Key', $Key) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'sign-local.ps1') @signArgs
    if ($LASTEXITCODE -ne 0) { Fail 'Building or signing failed. Nothing was flashed.' }
    foreach ($fw in ($toFlash.Env | Select-Object -Unique)) {
      $file = Get-ChildItem $signDir -Filter "$($products[$fw])-*.thfw" | Select-Object -First 1
      if (-not $file) { Fail "No signed package for $($products[$fw]) in $signDir." }
      $packages[$fw] = $file.FullName
    }
  }

  # Check every package's signature against the committed public key, then
  # strip the 128-byte signed header: the rest is the plain app image.
  $images = @{}
  foreach ($fw in $packages.Keys) {
    & $python $thfw verify $packages[$fw]
    if ($LASTEXITCODE -ne 0) { Fail "$($packages[$fw]) failed signature verification. Nothing was flashed." }
    $versions[$fw] = (& $python $thfw info $packages[$fw] | ConvertFrom-Json).version -join '.'
    $bytes = [IO.File]::ReadAllBytes($packages[$fw])
    $payload = New-Object byte[] ($bytes.Length - 128)
    [Array]::Copy($bytes, 128, $payload, 0, $payload.Length)
    $images[$fw] = Join-Path $work "$fw-$($versions[$fw]).bin"
    [IO.File]::WriteAllBytes($images[$fw], $payload)
  }

  # An all-0xFF OTA-selection sector makes the bootloader start the app slot we write.
  $blankBytes = New-Object byte[] 8192
  for ($i = 0; $i -lt $blankBytes.Length; $i++) { $blankBytes[$i] = 255 }
  $blank = Join-Path $work 'otadata-blank.bin'
  [IO.File]::WriteAllBytes($blank, $blankBytes)

  if ($DryRun) {
    Write-Host "`nDry run: $label verified and extracted; nothing was written to a board." -ForegroundColor Yellow
    exit 0
  }

  foreach ($row in $toFlash) {
    Write-Host "`n== Flashing $($row.Env) $($versions[$row.Env]) ($label) to $($row.Board) on $($row.Port)" -ForegroundColor Cyan
    # 921600 baud, as platformio.ini's upload_speed; esptool's default is 115200.
    & $pio pkg exec -p tool-esptoolpy -- esptool.py --port $row.Port --chip esp32 --baud 921600 write_flash 0xe000 $blank 0x10000 $images[$row.Env]
    $row.Result = if ($LASTEXITCODE -eq 0) { "flashed $($versions[$row.Env]) ($label)" } else { 'UPLOAD FAILED' }
  }

  Write-Host "`n== Summary" -ForegroundColor Cyan
  $bad = 0
  foreach ($row in $plan) {
    $text = if ($row.Result) { $row.Result } else { "skipped ($($row.Why))" }
    $color = if ($row.Result -like 'flashed*') { 'Green' } elseif ($row.Result) { 'Red' } else { 'Yellow' }
    if ($row.Result -and $row.Result -notlike 'flashed*') { $bad++ }
    Write-Host ("  {0,-12} {1,-18} {2}" -f $row.Board, $row.Mac, $text) -ForegroundColor $color
  }
  exit $(if ($bad) { 1 } else { 0 })
}

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
