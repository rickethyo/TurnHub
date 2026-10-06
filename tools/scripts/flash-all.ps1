<#
.SYNOPSIS
  One-button flash: pulls the latest repo, then flashes every attached TurnHub
  board with the firmware its MAC address says it should run.

.DESCRIPTION
  Boards are identified by MAC, never by COM port (ports change whenever the PC
  restarts). The MAC -> firmware table is tools\boards.local.md: this PC's
  boards, written by board setup and never committed. The table format, with
  made-up example rows, is in Documentation/engineering/BOARD_INVENTORY.md.
  Reading a MAC resets the board.

  - Unknown MACs are reported, and board setup is offered for them.
  - The test harness is never flashed; a spare Sigil gets the inert spare firmware.
  - A dirty working tree stops the pull so local work is never overwritten.

.PARAMETER Setup    Board setup only: identify every attached board, ask what each new one is
                    (suggesting Atlas for a CH340 bridge, and the E-ink or OLED Sigil its
                    firmware reports for a CP210x), and record it in tools\boards.local.md.
                    Then list every recorded board to rename, change type, make spare,
                    return to service or delete. No pull, no build, no flash.
                    setup-boards.cmd runs this mode.
.PARAMETER NoSetup  Never offer board setup; unknown boards are just skipped.
.PARAMETER NoPull   Flash the working copy as it is, without fetching.
.PARAMETER DryRun   Identify boards and show the plan; build and flash nothing. With -Setup,
                    show each change setup would record without writing it.
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
.PARAMETER Force    Write every board, even one whose flash already matches (see below).

  Before writing, each board compares its flash with what would be written (esptool
  verify_flash: bootloader, partition table, OTA selector, app). A board that already
  matches exactly is skipped as "unchanged".

  -Signed and -Sign flash fully, like the unsigned build: the bootloader and partition
  table (from this working copy's PlatformIO build), a blank OTA-selection sector and the
  signed app image, so a blank or erased board starts too. Settings, profiles and pairings
  in NVS are kept. Downgrades work over USB.
#>
param(
  [switch]$Setup,
  [switch]$NoSetup,
  [switch]$NoPull,
  [switch]$DryRun,
  [switch]$Signed,
  [string]$Release = 'latest',
  [switch]$Sign,
  [string]$Key,
  [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$localInventoryPath = Join-Path $root 'tools\boards.local.md'
$utf8 = New-Object System.Text.UTF8Encoding $false

# Firmware column in the inventory -> PlatformIO project folder and environment.
$targets = @{
  'atlas'      = @{ Dir = 'Atlas'; Env = 'atlas' }
  'sigil'      = @{ Dir = 'Sigil'; Env = 'sigil' }
  'sigil-oled' = @{ Dir = 'Sigil'; Env = 'sigil-oled' }
  # A spare Sigil (any `spare` row on a Sigil): inert, brought back over the air.
  'sigil-spare' = @{ Dir = 'Sigil'; Env = 'sigil-spare' }
}

# What board setup can record: menu key -> firmware column and default name.
$boardKinds = [ordered]@{
  '1' = @{ Firmware = 'atlas';      Name = 'Atlas';       Label = 'Atlas (table controller)' }
  '2' = @{ Firmware = 'sigil';      Name = 'E-ink Sigil'; Label = 'E-ink Sigil (GPIO4 open)' }
  '3' = @{ Firmware = 'sigil-oled'; Name = 'OLED Sigil';  Label = 'OLED Sigil (GPIO4 to GND)' }
  '4' = @{ Firmware = 'spare';      Name = 'Spare board'; Label = 'Spare (a Sigil gets the inert spare firmware)' }
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
    $cells = @($line.Trim().Trim('|').Split('|') | ForEach-Object { $_.Trim().Trim('`') })
    if ($cells.Count -lt 4) { continue }
    if ($cells[3] -notmatch '^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$') { continue }
    $rows += [pscustomobject]@{
      Board = $cells[0]; Firmware = $cells[1].ToLower(); Bridge = $cells[2]; Mac = $cells[3].ToUpper()
      Notes = if ($cells.Count -gt 4) { $cells[4] } else { '' }; Source = $source
    }
  }
  return $rows
}

function Read-Inventory {
  return @(Read-InventoryFile $localInventoryPath 'boards.local.md')
}

function Get-SerialPorts {
  # USB serial adapters only: a CH340 bridge is an Atlas, a CP210x a Sigil (or the old harness).
  Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.Name -match '\((COM\d+)\)' -and $_.PNPDeviceID -like 'USB\*' } |
    ForEach-Object {
      # Read the port here, before the VID tests (each successful -match replaces $Matches).
      $port = [regex]::Match($_.Name, '\((COM\d+)\)').Groups[1].Value
      $bridge = if ($_.PNPDeviceID -match 'VID_1A86') { 'CH340' }
                elseif ($_.PNPDeviceID -match 'VID_10C4') { 'CP210x' }
                else { 'other' }
      [pscustomobject]@{ Port = $port; Name = $_.Name; Bridge = $bridge }
    } |
    Sort-Object { [int]($_.Port -replace '\D') }
}

function Read-Mac($pio, $port) {
  try { $out = & $pio pkg exec -p tool-esptoolpy -- esptool.py --port $port read_mac 2>&1 | Out-String } catch { return $null }
  if ($out -match 'MAC:\s*([0-9A-Fa-f:]{17})') { return $Matches[1].ToUpper() }
  return $null
}

function Read-BootKind($port, [int]$seconds = 5, [switch]$Identify) {
  # Resets the board the way esptool does (EN pulled low through RTS, IO0 left high) and
  # listens to its boot log. A Sigil reports its GPIO4 strap even when it is running the
  # wrong display build, and so does the spare firmware. Returns 'eink', 'oled',
  # 'spare-eink', 'spare-oled', 'atlas', 'harness', or $null. With -Identify, a
  # board that answered is told to blink its ring or LED white for 10 s.
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
      $kind = $null
      if ($log -match 'SIGIL\|SPARE\|(OLED|EINK)') { $kind = 'spare-' + $Matches[1].ToLower() }
      elseif ($log -match 'SIGIL\|HW\|MISMATCH\|BOARD\|(OLED|EINK)') { return $Matches[1].ToLower() }
      elseif ($log -match 'SIGIL\|HW\|(OLED|EINK)') { $kind = $Matches[1].ToLower() }
      elseif ($log -match 'ATLAS\|BOOT\|') { $kind = 'atlas' }
      elseif ($log -match 'HARNESS\|BOOT') { return 'harness' }
      if ($kind) {
        if ($Identify) {
          # Wait for the firmware's loop to start reading serial, then ask it to blink.
          $ready = if ($kind -eq 'atlas') { 'ATLAS\|DIAGNOSTICS\|' } else { 'SIGIL\|READY' }
          while ($log -notmatch $ready -and (Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 100
            $log += $sp.ReadExisting()
          }
          Start-Sleep -Milliseconds 300
          $sp.WriteLine('identify')
          Start-Sleep -Milliseconds 100
        }
        return $kind
      }
    }
  } catch {
  } finally { $sp.Close() }
  return $null
}

function Send-Identify($port) {
  # Asks a running TurnHub board to blink white for 10 s, without resetting it.
  $sp = New-Object System.IO.Ports.SerialPort $port, 115200
  $sp.DtrEnable = $false
  $sp.RtsEnable = $false
  try { $sp.Open(); $sp.WriteLine('identify'); Start-Sleep -Milliseconds 100 } catch { } finally { $sp.Close() }
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
    elseif ($match.Firmware -like 'spare*' -and $match.Firmware -ne 'spare:atlas' -and $b.Bridge -ne 'CH340') {
      $action = 'flash'; $fw = 'sigil-spare'
    }
    elseif (-not $targets.ContainsKey($match.Firmware) -or $match.Firmware -eq 'sigil-spare') {
      $why = "$($match.Board): never flashed by this script"
    }
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

function Get-BlankOtadata {
  # An all-0xFF OTA-selection sector makes the bootloader start the app at 0x10000.
  $path = Join-Path $env:TEMP 'turnhub-otadata-blank.bin'
  if (-not (Test-Path $path)) {
    $bytes = New-Object byte[] 8192
    for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = 255 }
    [IO.File]::WriteAllBytes($path, $bytes)
  }
  return $path
}

function Test-FlashMatches($port, $regions) {
  # True when the board's flash already holds exactly these images ($regions:
  # address, file, address, file...). The chip hashes its own flash, so nothing
  # is written; it resets the board. -Force skips the check.
  if ($Force) { return $false }
  Write-Host "  Comparing $port's flash with the new images..."
  # A mismatch is reported on stderr, which Stop would turn into an exception.
  try {
    $out = & $pio pkg exec -p tool-esptoolpy -- esptool.py --port $port --chip esp32 --baud 921600 verify_flash @regions 2>&1 | Out-String
  } catch { return $false }
  return $LASTEXITCODE -eq 0 -and $out -match 'verify OK' -and $out -notmatch 'verify FAILED'
}

function Exit-WithSummary($plan) {
  Write-Host "`n== Summary" -ForegroundColor Cyan
  $bad = 0
  foreach ($row in $plan) {
    $text = if ($row.Result) { $row.Result } else { "skipped ($($row.Why))" }
    $ok = $row.Result -like 'flashed*' -or $row.Result -like 'unchanged*'
    $color = if ($ok) { 'Green' } elseif ($row.Result) { 'Red' } else { 'Yellow' }
    if ($row.Result -and -not $ok) { $bad++ }
    Write-Host ("  {0,-12} {1,-18} {2}" -f $row.Board, $row.Mac, $text) -ForegroundColor $color
  }
  exit $(if ($bad) { 1 } else { 0 })
}

$localHeader = @(
  '# Boards on this PC',
  '',
  'Written by board setup (`tools\setup-boards.cmd`); flash-all flashes each attached board',
  'with the firmware its MAC has here. This file is not committed. Firmware is `atlas`,',
  '`sigil` (E-ink), `sigil-oled`, or `spare` / `spare:<firmware it ran>` (a Sigil gets the',
  'inert spare firmware; see SPARE_SIGIL.md). Run board setup again to rename, retype,',
  'retire or delete a board.',
  '',
  '| Board | Firmware | USB bridge | MAC | Notes |',
  '|---|---|---|---|---|'
)
$kindKeys = @{ 'atlas' = '1'; 'sigil' = '2'; 'sigil-oled' = '3' }

function Get-KindLabel($firmware) {
  if ($firmware -match '^spare:(.+)$') { return "Spare (was $(Get-KindLabel $Matches[1]))" }
  switch ($firmware) {
    'atlas'      { return 'Atlas' }
    'sigil'      { return 'E-ink Sigil' }
    'sigil-oled' { return 'OLED Sigil' }
    'spare'      { return 'Spare' }
  }
  return "$firmware (never flashed)"
}

function Format-Row($name, $firmware, $bridge, $mac, $notes) {
  '| {0} | `{1}` | {2} | `{3}` | {4} |' -f $name, $firmware, $bridge, $mac, $notes
}

function Save-LocalRow($mac, $row) {
  # Replaces or adds this MAC's row in tools\boards.local.md, or removes it when $row is $null.
  if ($DryRun) {
    $what = if ($row) { $row } else { "remove $mac" }
    Write-Host "  Dry run, not written: $what" -ForegroundColor Yellow
    return
  }
  $lines = if (Test-Path $localInventoryPath) { @([IO.File]::ReadAllLines($localInventoryPath, $utf8)) } else { $localHeader }
  $out = @(); $done = $false
  foreach ($l in $lines) {
    if ($l -match ('^\|([^|]*\|){3}\s*`?' + [regex]::Escape($mac) + '`?\s*\|')) {
      if ($row -and -not $done) { $out += $row; $done = $true }
      continue
    }
    $out += $l
  }
  if ($row -and -not $done) { $out += $row }
  [IO.File]::WriteAllLines($localInventoryPath, [string[]]$out, $utf8)
}

function Get-Suggestion($bridge, $port) {
  # What an attached board probably is: its USB bridge, then its firmware's boot
  # log. A board running TurnHub firmware also blinks white for 10 s.
  Write-Host '  Listening to its boot log (this resets it)...'
  $kind = Read-BootKind $port -Identify
  if ($kind -and $kind -ne 'harness') { Write-Host '  Its ring or LED is blinking white.' -ForegroundColor Cyan }
  if ($bridge -eq 'CH340') { return @{ Key = '1'; Hint = 'CH340 bridge: an Atlas' } }
  switch ($kind) {
    'eink'    { return @{ Key = '2'; Hint = 'its firmware reports an E-ink Sigil (GPIO4 open)' } }
    'oled'    { return @{ Key = '3'; Hint = 'its firmware reports an OLED Sigil (GPIO4 to GND)' } }
    'spare-eink' { return @{ Key = '2'; Hint = 'it runs the spare firmware; its strap says E-ink Sigil (GPIO4 open)' } }
    'spare-oled' { return @{ Key = '3'; Hint = 'it runs the spare firmware; its strap says OLED Sigil (GPIO4 to GND)' } }
    'atlas'   { return @{ Key = '1'; Hint = 'its firmware reports an Atlas' } }
    'harness' { return @{ Key = $null; Hint = 'this is the retired test harness: record it as Spare or skip it' } }
  }
  if ($bridge -eq 'CP210x') {
    return @{ Key = $null; Hint = 'CP210x bridge: a Sigil; no TurnHub firmware answered, so check its GPIO4 strap (open = E-ink, GND = OLED)' }
  }
  return @{ Key = $null; Hint = 'unrecognised USB bridge and no TurnHub firmware answered' }
}

function Read-Kind($suggest, [switch]$AllowSpare) {
  # Asks for a board type; returns its menu key, or $null to skip or go back.
  foreach ($k in $boardKinds.Keys) {
    if ($k -eq '4' -and -not $AllowSpare) { continue }
    $mark = if ($k -eq $suggest) { '  (suggested)' } else { '' }
    Write-Host ("    {0}  {1}{2}" -f $k, $boardKinds[$k].Label, $mark)
  }
  $last = if ($AllowSpare) { '4' } else { '3' }
  $prompt = if ($suggest) { "  Type? Enter for $suggest, S to skip" } else { "  Type? 1-$last, S to skip" }
  while ($true) {
    $answer = "$(Read-Host $prompt)".Trim().ToUpper()
    if (-not $answer -and $suggest) { return $suggest }
    if ($answer -eq 'S') { return $null }
    if ($boardKinds.Contains($answer) -and ($AllowSpare -or $answer -ne '4')) { return $answer }
  }
}

function Get-FreeName($base, $inventory) {
  $taken = @($inventory | ForEach-Object Board)
  $name = $base; $n = 2
  while ($taken -contains $name) { $name = "$base $n"; $n++ }
  return $name
}

function Invoke-BoardSetup($newBoards) {
  # Asks what each new board is and records it in tools\boards.local.md. Returns $true when any was.
  $any = $false
  foreach ($b in $newBoards) {
    Write-Host ("`n  New board: {0}  {1}  ({2} bridge)" -f $b.Port, $b.Mac, $b.Bridge) -ForegroundColor Cyan
    $s = Get-Suggestion $b.Bridge $b.Port
    Write-Host "  $($s.Hint)"
    $key = Read-Kind $s.Key -AllowSpare
    if (-not $key) { Write-Host '  Skipped.'; continue }
    $kind = $boardKinds[$key]
    $default = Get-FreeName $kind.Name @(Read-Inventory)
    $name = "$(Read-Host "  Name for this board, Enter for '$default'")".Trim()
    if (-not $name) { $name = $default }
    $name = $name.Replace('|', '/')
    Save-LocalRow $b.Mac (Format-Row $name $kind.Firmware $b.Bridge $b.Mac "Added by board setup $(Get-Date -Format 'yyyy-MM-dd')")
    Write-Host "  $name -> $(Get-KindLabel $kind.Firmware)" -ForegroundColor Green
    $any = $true
  }
  return $any
}

function Invoke-BoardManager($attached) {
  # Lists every recorded board and lets you rename, retype, retire, return or delete one.
  while ($true) {
    $inventory = @(Read-Inventory)
    if (-not $inventory.Count) { Write-Host "`nNo boards are recorded yet."; return }
    Write-Host "`n== Boards" -ForegroundColor Cyan
    for ($i = 0; $i -lt $inventory.Count; $i++) {
      $r = $inventory[$i]
      $on = @($attached | Where-Object Mac -eq $r.Mac)
      $where = if ($on.Count) { "on $($on[0].Port)" } else { '' }
      Write-Host ("  {0,2}  {1,-24} {2,-30} {3}  {4}" -f ($i + 1), $r.Board, (Get-KindLabel $r.Firmware), $r.Mac, $where)
    }
    $pick = "$(Read-Host "`nPick a board to change (1-$($inventory.Count)), or Enter to finish")".Trim()
    if (-not $pick) { return }
    $n = 0
    if (-not [int]::TryParse($pick, [ref]$n) -or $n -lt 1 -or $n -gt $inventory.Count) { continue }
    $r = $inventory[$n - 1]
    $picked = @($attached | Where-Object Mac -eq $r.Mac)
    if ($picked.Count) {
      Send-Identify $picked[0].Port
      Write-Host "  If $($r.Board) runs TurnHub firmware, its ring or LED is blinking white." -ForegroundColor Cyan
    }
    $spare = $r.Firmware -like 'spare*'
    $inService = $kindKeys.ContainsKey($r.Firmware)
    Write-Host "`n  $($r.Board): $(Get-KindLabel $r.Firmware)" -ForegroundColor Cyan
    Write-Host '    N  Rename'
    if ($spare) { Write-Host '    S  Return to service' }
    else {
      Write-Host '    T  Change type'
      if ($inService) { Write-Host '    P  Make spare (a Sigil gets the inert spare firmware at the next flash)' }
    }
    Write-Host '    D  Delete: erase it if attached, and forget it'
    $act = "$(Read-Host '  Choose, or Enter to go back')".Trim().ToUpper()

    $name = $r.Board; $firmware = $r.Firmware
    if ($act -eq 'N') {
      $new = "$(Read-Host "  New name for '$name'")".Trim()
      if (-not $new) { continue }
      $name = $new.Replace('|', '/')
    } elseif ($act -eq 'T' -and -not $spare) {
      $key = Read-Kind $kindKeys[$r.Firmware]
      if (-not $key) { continue }
      $firmware = $boardKinds[$key].Firmware
    } elseif ($act -eq 'P' -and $inService) {
      $firmware = "spare:$($r.Firmware)"
    } elseif ($act -eq 'S' -and $spare) {
      $suggest = $null
      if ($r.Firmware -match '^spare:(.+)$') { $suggest = $kindKeys[$Matches[1]] }
      $on = @($attached | Where-Object Mac -eq $r.Mac)
      if (-not $suggest -and $on.Count) {
        $s = Get-Suggestion $on[0].Bridge $on[0].Port
        Write-Host "  $($s.Hint)"
        $suggest = $s.Key
      }
      $key = Read-Kind $suggest
      if (-not $key) { continue }
      $firmware = $boardKinds[$key].Firmware
    } elseif ($act -eq 'D') {
      # Delete wipes an attached board clean (firmware, pairing, settings) and
      # forgets it; a board that is not attached is only forgotten.
      $on = @($attached | Where-Object Mac -eq $r.Mac)
      $what = if ($on.Count) { "erase ALL flash on $name ($($on[0].Port)) and forget it" } else { "forget $name (not attached, so it is not erased)" }
      $sure = "$(Read-Host "  This will $what. Type y to confirm")".Trim()
      if ($sure -notmatch '^[Yy]$') { continue }
      if ($on.Count) {
        if ($DryRun) {
          Write-Host "  Dry run, not erased: $name on $($on[0].Port)" -ForegroundColor Yellow
        } else {
          Write-Host "  Erasing $name on $($on[0].Port)..."
          & $pio pkg exec -p tool-esptoolpy -- esptool.py --port $on[0].Port erase_flash
          if ($LASTEXITCODE -ne 0) { Write-Host "  Erase failed; $name is still on the list." -ForegroundColor Red; continue }
        }
      }
      Save-LocalRow $r.Mac $null
      Write-Host "  Deleted $name." -ForegroundColor Green
      continue
    } else { continue }

    Save-LocalRow $r.Mac (Format-Row $name $firmware $r.Bridge $r.Mac $r.Notes)
    Write-Host "  $name -> $(Get-KindLabel $firmware)" -ForegroundColor Green
  }
}

function Invoke-PioFlash($rows) {
  # Builds each firmware once with PlatformIO, then uploads it to each board.
  foreach ($fw in ($rows.Env | Select-Object -Unique)) {
    $t = $targets[$fw]
    Write-Host "`n== Building $fw" -ForegroundColor Cyan
    Push-Location (Join-Path $root $t.Dir)
    & $pio run -e $t.Env
    $ok = $LASTEXITCODE -eq 0
    Pop-Location
    if (-not $ok) {
      foreach ($row in $rows | Where-Object Env -eq $fw) { $row.Result = 'BUILD FAILED' }
    }
  }

  # PlatformIO writes the bootloader, partition table, its boot_app0 OTA selector
  # and the app; a board already holding all four is skipped.
  $bootApp0 = Join-Path $env:USERPROFILE '.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin'
  foreach ($row in $rows) {
    if ($row.Result) { continue }
    $t = $targets[$row.Env]
    $buildDir = Join-Path $root "$($t.Dir)\.pio\build\$($t.Env)"
    $regions = @('0x1000', (Join-Path $buildDir 'bootloader.bin'), '0x8000', (Join-Path $buildDir 'partitions.bin'),
      '0xe000', $bootApp0, '0x10000', (Join-Path $buildDir 'firmware.bin'))
    $files = @($regions[1], $regions[3], $regions[5], $regions[7])
    if (-not ($files | Where-Object { -not (Test-Path $_) }) -and (Test-FlashMatches $row.Port $regions)) {
      $row.Result = 'unchanged (already on the board)'
      continue
    }
    Write-Host "`n== Flashing $($row.Board) ($($row.Env)) on $($row.Port)" -ForegroundColor Cyan
    Push-Location (Join-Path $root $t.Dir)
    & $pio run -e $t.Env --target upload --upload-port $row.Port
    $row.Result = if ($LASTEXITCODE -eq 0) { 'flashed' } else { 'UPLOAD FAILED' }
    Pop-Location
  }
}

# --- 0. board setup only (-Setup) -------------------------------------------------
if ($Setup) {
  $ports = @(Get-SerialPorts)
  $plan = @()
  if ($ports.Count) {
    $pio = Find-Pio
    $plan = @(Get-Plan @(Get-Boards $pio $ports) @(Read-Inventory))
    foreach ($row in $plan | Where-Object Known) {
      Write-Host ("  {0,-6} {1,-18} {2} ({3})" -f $row.Port, $row.Mac, $row.Board, (Get-KindLabel $row.Firmware))
    }
    foreach ($row in $plan | Where-Object { -not $_.Mac }) {
      Write-Host ("  {0,-6} could not read its MAC (busy? close any serial monitor)" -f $row.Port) -ForegroundColor Yellow
    }
    $new = @($plan | Where-Object { $_.Mac -and -not $_.Known })
    if ($new.Count) { Invoke-BoardSetup $new | Out-Null }
  } else {
    Write-Host 'No USB serial boards are connected; you can still change the recorded ones.' -ForegroundColor Yellow
  }
  Invoke-BoardManager $plan
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
    if (Invoke-BoardSetup $new) {
      $inventory = @(Read-Inventory)
      $plan = @(Get-Plan $boards $inventory)
      Write-Host "`n== Plan" -ForegroundColor Cyan
      Show-Plan $plan
    }
  }
}

# A spare returned to service over the air (SPARE_SIGIL.md) runs normal Sigil
# firmware again: record that and flash it as that Sigil, never back to spare.
foreach ($row in $plan | Where-Object Env -eq 'sigil-spare') {
  Write-Host "  Checking spare $($row.Board) on $($row.Port) (this resets it)..."
  $kind = Read-BootKind $row.Port
  if ($kind -ne 'eink' -and $kind -ne 'oled') { continue }
  $fw = if ($kind -eq 'oled') { 'sigil-oled' } else { 'sigil' }
  $entry = $inventory | Where-Object Mac -eq $row.Mac | Select-Object -First 1
  Save-LocalRow $row.Mac (Format-Row $row.Board $fw $entry.Bridge $row.Mac $entry.Notes)
  Write-Host "  $($row.Board) is running $(Get-KindLabel $fw) firmware (returned to service over the air): recorded." -ForegroundColor Green
  $row.Firmware = $fw; $row.Env = $fw
}

$toFlash = @($plan | Where-Object Action -eq 'flash')
if (-not $toFlash.Count) { Fail 'No connected board is set up for a flashable firmware. Run setup-boards.cmd to add new boards.' }
if ($DryRun -and -not ($Signed -or $Sign)) { Write-Host "`nDry run: nothing was built or flashed." -ForegroundColor Yellow; exit 0 }

# --- 2b. signed images over USB (-Signed release, -Sign local key) ------------------
if ($Signed -or $Sign) {
  # The spare firmware is never packaged: spares get a plain PlatformIO upload.
  $spareRows = @($toFlash | Where-Object Env -eq 'sigil-spare')
  $toFlash = @($toFlash | Where-Object Env -ne 'sigil-spare')
  if (-not $toFlash.Count) {
    if ($DryRun) { Write-Host "`nDry run: nothing was built or flashed." -ForegroundColor Yellow; exit 0 }
    Invoke-PioFlash $spareRows
    Exit-WithSummary $plan
  }
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

  $blank = Get-BlankOtadata

  # Flash fully, like the unsigned build, so a blank or erased board starts too:
  # the bootloader and partition table come from this working copy's PlatformIO
  # build (sign-local.ps1 just made it with -Sign; built now if missing).
  $boot = @{}
  foreach ($fw in ($toFlash.Env | Select-Object -Unique)) {
    $t = $targets[$fw]
    $buildDir = Join-Path $root "$($t.Dir)\.pio\build\$($t.Env)"
    $bootloader = Join-Path $buildDir 'bootloader.bin'
    $partitions = Join-Path $buildDir 'partitions.bin'
    if (-not (Test-Path $bootloader) -or -not (Test-Path $partitions)) {
      Write-Host "`n== Building $fw for its bootloader and partition table" -ForegroundColor Cyan
      & $pio run --project-dir (Join-Path $root $t.Dir) --environment $t.Env
      if ($LASTEXITCODE -ne 0 -or -not (Test-Path $bootloader) -or -not (Test-Path $partitions)) {
        Fail "Could not build $fw's bootloader and partition table. Nothing was flashed."
      }
    }
    $boot[$fw] = @{ Bootloader = $bootloader; Partitions = $partitions }
  }

  if ($DryRun) {
    Write-Host "`nDry run: $label verified and extracted; nothing was written to a board." -ForegroundColor Yellow
    exit 0
  }

  if ($spareRows.Count) { Invoke-PioFlash $spareRows }
  foreach ($row in $toFlash) {
    Write-Host "`n== Flashing $($row.Env) $($versions[$row.Env]) ($label) to $($row.Board) on $($row.Port)" -ForegroundColor Cyan
    # 921600 baud, as platformio.ini's upload_speed; esptool's default is 115200.
    $b = $boot[$row.Env]
    if (Test-FlashMatches $row.Port @('0x1000', $b.Bootloader, '0x8000', $b.Partitions, '0xe000', $blank, '0x10000', $images[$row.Env])) {
      $row.Result = "unchanged ($($versions[$row.Env]) already on the board)"
      continue
    }
    & $pio pkg exec -p tool-esptoolpy -- esptool.py --port $row.Port --chip esp32 --baud 921600 write_flash `
      0x1000 $b.Bootloader 0x8000 $b.Partitions 0xe000 $blank 0x10000 $images[$row.Env]
    $row.Result = if ($LASTEXITCODE -eq 0) { "flashed $($versions[$row.Env]) ($label)" } else { 'UPLOAD FAILED' }
  }

  Exit-WithSummary $plan
}

# --- 3. build once per firmware, then flash each board --------------------------
Invoke-PioFlash $toFlash

# --- 4. summary -----------------------------------------------------------------
Exit-WithSummary $plan
