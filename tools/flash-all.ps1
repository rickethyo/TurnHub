<#
.SYNOPSIS
  One-button flash: pulls the latest repo, then flashes every attached TurnHub
  board with the firmware its MAC address says it should run.

.DESCRIPTION
  Boards are identified by MAC, never by COM port (ports change whenever the PC
  restarts). The MAC -> firmware table is read from tools\boards.local.md (this
  PC's boards, written by board setup, not committed) and then from
  Documentation/engineering/BOARD_INVENTORY.md; a MAC in the local file wins.
  Reading a MAC resets the board.

  - Unknown MACs are reported, and board setup is offered for them.
  - The test harness and spare boards are never flashed.
  - A dirty working tree stops the pull so local work is never overwritten.

.PARAMETER Setup    Board setup only: identify every attached board, ask what each new one is
                    (suggesting Atlas for a CH340 bridge, and the E-ink or OLED Sigil its
                    firmware reports for a CP210x), and record it in tools\boards.local.md.
                    No pull, no build, no flash. setup-boards.cmd runs this mode.
.PARAMETER NoSetup  Never offer board setup; unknown boards are just skipped.
.PARAMETER NoPull   Flash the working copy as it is, without fetching.
.PARAMETER DryRun   Identify boards and show the plan; build and flash nothing. With -Setup,
                    show the rows setup would record without writing them.
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
  [switch]$Setup,
  [switch]$NoSetup,
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
$localInventoryPath = Join-Path $PSScriptRoot 'boards.local.md'
$utf8 = New-Object System.Text.UTF8Encoding $false

# Firmware column in the inventory -> PlatformIO project folder and environment.
$targets = @{
  'atlas'      = @{ Dir = 'Atlas'; Env = 'atlas' }
  'sigil'      = @{ Dir = 'Sigil'; Env = 'sigil' }
  'sigil-oled' = @{ Dir = 'Sigil'; Env = 'sigil-oled' }
}

# What board setup can record: menu key -> firmware column and default name.
$boardKinds = [ordered]@{
  '1' = @{ Firmware = 'atlas';      Name = 'Atlas';       Label = 'Atlas (table controller)' }
  '2' = @{ Firmware = 'sigil';      Name = 'E-ink Sigil'; Label = 'E-ink Sigil (GPIO4 open)' }
  '3' = @{ Firmware = 'sigil-oled'; Name = 'OLED Sigil';  Label = 'OLED Sigil (GPIO4 to GND)' }
  '4' = @{ Firmware = 'spare';      Name = 'Spare board'; Label = 'Spare (recorded, never flashed)' }
}

function Fail($message) { Write-Host "`nERROR: $message" -ForegroundColor Red; exit 1 }

function Find-Pio {
  $cmd = Get-Command pio -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  $penv = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
  if (Test-Path $penv) { return $penv }
  Fail 'PlatformIO (pio) was not found. Install it or add %USERPROFILE%\.platformio\penv\Scripts to PATH.'
}

function Read-InventoryFile($path, $source) {
  $rows = @()
  if (-not (Test-Path $path)) { return $rows }
  foreach ($line in [IO.File]::ReadAllLines($path, $utf8)) {
    if ($line -notmatch '^\|') { continue }
    $cells = $line.Trim().Trim('|').Split('|') | ForEach-Object { $_.Trim().Trim('`') }
    if ($cells.Count -lt 4) { continue }
    if ($cells[3] -notmatch '^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$') { continue }
    $rows += [pscustomobject]@{ Board = $cells[0]; Firmware = $cells[1].ToLower(); Mac = $cells[3].ToUpper(); Source = $source }
  }
  return $rows
}

function Read-Inventory {
  # This PC's boards first, so a local row overrides the shared record for the same MAC.
  $rows = @(Read-InventoryFile $localInventoryPath 'boards.local.md')
  foreach ($row in Read-InventoryFile $inventoryPath 'BOARD_INVENTORY.md') {
    if (-not ($rows | Where-Object Mac -eq $row.Mac)) { $rows += $row }
  }
  return $rows
}

function Get-SerialPorts {
  # USB serial adapters only: a CH340 bridge is an Atlas, a CP210x a Sigil (or the old harness).
  Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.Name -match '\((COM\d+)\)' -and $_.PNPDeviceID -like 'USB\*' } |
    ForEach-Object {
      $bridge = if ($_.PNPDeviceID -match 'VID_1A86') { 'CH340' }
                elseif ($_.PNPDeviceID -match 'VID_10C4') { 'CP210x' }
                else { 'other' }
      [pscustomobject]@{ Port = $Matches[1]; Name = $_.Name; Bridge = $bridge }
    } |
    Sort-Object { [int]($_.Port -replace '\D') }
}

function Read-Mac($pio, $port) {
  $out = & $pio pkg exec -p tool-esptoolpy -- esptool.py --port $port read_mac 2>&1 | Out-String
  if ($out -match 'MAC:\s*([0-9A-Fa-f:]{17})') { return $Matches[1].ToUpper() }
  return $null
}

function Read-BootKind($port, [int]$seconds = 5) {
  # Resets the board the way esptool does (EN pulled low through RTS, IO0 left high) and
  # listens to its boot log. A Sigil reports its GPIO4 strap even when it is running the
  # wrong display build. Returns 'eink', 'oled', 'atlas', 'harness', or $null.
  $sp = New-Object System.IO.Ports.SerialPort $port, 115200
  $sp.DtrEnable = $false
  $sp.RtsEnable = $true
  try { $sp.Open() } catch { return $null }
  try {
    Start-Sleep -Milliseconds 100
    $sp.RtsEnable = $false
    $log = ''
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
      Start-Sleep -Milliseconds 100
      $log += $sp.ReadExisting()
      if ($log -match 'SIGIL\|HW\|(MISMATCH\|BOARD\|)?(OLED|EINK)') { return $Matches[2].ToLower() }
      if ($log -match 'ATLAS\|BOOT\|') { return 'atlas' }
      if ($log -match 'HARNESS\|BOOT') { return 'harness' }
    }
  } catch {
  } finally { $sp.Close() }
  return $null
}

function Get-Boards($pio, $ports) {
  Write-Host "`n== Identifying $($ports.Count) connected board(s) by MAC" -ForegroundColor Cyan
  foreach ($p in $ports) {
    [pscustomobject]@{ Port = $p.Port; Bridge = $p.Bridge; Mac = (Read-Mac $pio $p.Port) }
  }
}

function Get-Plan($boards, $inventory) {
  foreach ($b in $boards) {
    $match = if ($b.Mac) { $inventory | Where-Object { $_.Mac -eq $b.Mac } | Select-Object -First 1 }
    $action = 'skip'; $fw = ''; $why = ''
    if (-not $b.Mac) { $why = 'could not read MAC (busy? close any serial monitor)' }
    elseif (-not $match) { $why = 'new board: not set up yet (run setup-boards.cmd)' }
    elseif (-not $targets.ContainsKey($match.Firmware)) { $why = "$($match.Board): never flashed by this script" }
    else { $action = 'flash'; $fw = $match.Firmware }
    [pscustomobject]@{
      Port = $b.Port; Bridge = $b.Bridge; Mac = $b.Mac; Board = if ($match) { $match.Board } else { '?' }
      Known = [bool]$match; Firmware = if ($match) { $match.Firmware } else { '' }; Action = $action; Env = $fw; Why = $why; Result = ''
    }
  }
}

function Show-Plan($plan) {
  foreach ($row in $plan) {
    $label = if ($row.Action -eq 'flash') { "-> $($row.Env)" } else { "-> skipped ($($row.Why))" }
    Write-Host ("  {0,-6} {1,-18} {2,-12} {3}" -f $row.Port, $row.Mac, $row.Board, $label)
  }
}

function Invoke-BoardSetup($newBoards, $inventory) {
  # Asks what each new board is and appends a row per answer to tools\boards.local.md.
  $taken = @($inventory | ForEach-Object Board)
  $lines = @()
  foreach ($b in $newBoards) {
    Write-Host ("`n  {0}  {1}  ({2} bridge)" -f $b.Port, $b.Mac, $b.Bridge) -ForegroundColor Cyan
    $suggest = $null; $hint = ''
    if ($b.Bridge -eq 'CH340') { $suggest = '1'; $hint = 'CH340 bridge: an Atlas' }
    else {
      Write-Host '  Listening to its boot log (this resets it)...'
      switch (Read-BootKind $b.Port) {
        'eink'    { $suggest = '2'; $hint = 'its firmware reports an E-ink Sigil (GPIO4 open)' }
        'oled'    { $suggest = '3'; $hint = 'its firmware reports an OLED Sigil (GPIO4 to GND)' }
        'atlas'   { $suggest = '1'; $hint = 'its firmware reports an Atlas' }
        'harness' { $hint = 'this is the retired test harness: record it as Spare or skip it' }
        default   {
          $hint = if ($b.Bridge -eq 'CP210x') { 'CP210x bridge: a Sigil; no TurnHub firmware answered, so check its GPIO4 strap (open = E-ink, GND = OLED)' }
                  else { 'unrecognised USB bridge and no TurnHub firmware answered' }
        }
      }
    }
    Write-Host "  $hint"
    foreach ($k in $boardKinds.Keys) {
      $mark = if ($k -eq $suggest) { '  (suggested)' } else { '' }
      Write-Host ("    {0}  {1}{2}" -f $k, $boardKinds[$k].Label, $mark)
    }
    Write-Host '    S  Skip: do not record this board'
    $prompt = if ($suggest) { "  What is this board? Enter for $suggest" } else { '  What is this board? 1-4 or S' }
    do {
      $answer = "$(Read-Host $prompt)".Trim().ToUpper()
      if (-not $answer -and $suggest) { $answer = $suggest }
    } until ($answer -eq 'S' -or $boardKinds.Contains($answer))
    if ($answer -eq 'S') { Write-Host '  Skipped.'; continue }

    $kind = $boardKinds[$answer]
    $default = $kind.Name; $n = 2
    while ($taken -contains $default) { $default = "$($kind.Name) $n"; $n++ }
    $name = "$(Read-Host "  Name for this board, Enter for '$default'")".Trim()
    if (-not $name) { $name = $default }
    $name = $name.Replace('|', '/')
    $taken += $name
    $lines += ('| {0} | `{1}` | {2} | `{3}` | Added by board setup {4} |' -f $name, $kind.Firmware, $b.Bridge, $b.Mac, (Get-Date -Format 'yyyy-MM-dd'))
    Write-Host "  $name -> $($kind.Firmware)" -ForegroundColor Green
  }
  if (-not $lines.Count) { return $false }
  if ($DryRun) {
    Write-Host "`nDry run: these rows were not written:" -ForegroundColor Yellow
    $lines | ForEach-Object { Write-Host "  $_" }
    return $false
  }
  if (-not (Test-Path $localInventoryPath)) {
    $header = @(
      '# Boards on this PC',
      '',
      'Written by board setup (`tools\setup-boards.cmd`, or `flash-all.ps1 -Setup`). This file',
      'is not committed. flash-all reads it before `Documentation/engineering/BOARD_INVENTORY.md`,',
      'and a MAC listed here wins. Firmware is `atlas`, `sigil` (E-ink), `sigil-oled`, or `spare`',
      '(recorded but never flashed). Edit or delete a row to change or forget a board.',
      '',
      '| Board | Firmware | USB bridge | MAC | Notes |',
      '|---|---|---|---|---|'
    )
    [IO.File]::WriteAllLines($localInventoryPath, [string[]]$header, $utf8)
  }
  [IO.File]::AppendAllLines($localInventoryPath, [string[]]$lines, $utf8)
  Write-Host "`nRecorded $($lines.Count) board(s) in tools\boards.local.md." -ForegroundColor Green
  return $true
}

# --- 0. board setup only (-Setup) -------------------------------------------------
if ($Setup) {
  $pio = Find-Pio
  $ports = @(Get-SerialPorts)
  if (-not $ports.Count) { Fail 'No USB serial boards are connected.' }
  $inventory = @(Read-Inventory)
  $plan = @(Get-Plan @(Get-Boards $pio $ports) $inventory)
  foreach ($row in $plan | Where-Object Known) {
    Write-Host ("  {0,-6} {1,-18} already set up: {2} ({3})" -f $row.Port, $row.Mac, $row.Board, $row.Firmware)
  }
  foreach ($row in $plan | Where-Object { -not $_.Mac }) {
    Write-Host ("  {0,-6} could not read its MAC (busy? close any serial monitor)" -f $row.Port) -ForegroundColor Yellow
  }
  $new = @($plan | Where-Object { $_.Mac -and -not $_.Known })
  if (-not $new.Count) { Write-Host "`nEvery connected board is already set up." -ForegroundColor Green; exit 0 }
  Invoke-BoardSetup $new $inventory | Out-Null
  exit 0
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
$inventory = @(Read-Inventory)
$ports = @(Get-SerialPorts)
if (-not $ports.Count) { Fail 'No USB serial boards are connected.' }

$boards = @(Get-Boards $pio $ports)
$plan = @(Get-Plan $boards $inventory)
Show-Plan $plan

$new = @($plan | Where-Object { $_.Mac -and -not $_.Known })
if ($new.Count -and -not $NoSetup -and -not $DryRun) {
  $answer = "$(Read-Host "`n$($new.Count) board(s) are not set up yet. Set them up now? [Y/n]")"
  if ($answer.Trim() -notmatch '^[Nn]') {
    if (Invoke-BoardSetup $new $inventory) {
      $inventory = @(Read-Inventory)
      $plan = @(Get-Plan $boards $inventory)
      Write-Host "`n== Plan" -ForegroundColor Cyan
      Show-Plan $plan
    }
  }
}

$toFlash = @($plan | Where-Object Action -eq 'flash')
if (-not $toFlash.Count) { Fail 'No connected board is set up for a flashable firmware. Run setup-boards.cmd to add new boards.' }
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
