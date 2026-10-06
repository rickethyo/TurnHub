<#
.SYNOPSIS
  Sign-and-flash for the temporary laptop. Builds the working copy in .\Include,
  signs it with the new key (c76d7ddf) kept in %USERPROFILE%\TurnHub-Private,
  verifies it and flashes every attached board by MAC. A thin wrapper around
  Include\tools\flash-all.ps1 -Sign; it only supplies this laptop's key path,
  installs python 'cryptography' into PlatformIO's Python if it is missing, and
  refuses to run unless the checked-out firmware carries the new public key.

  This file lives outside the git repo on purpose. It holds a path, never the key.

.EXAMPLE
  flash-signed-here.cmd              pull (if clean), build, sign, flash
  flash-signed-here.cmd -DryRun      identify boards and verify; flash nothing
  flash-signed-here.cmd -NoPull      flash the working copy as it is
#>
param([switch]$NoPull, [switch]$DryRun)

$ErrorActionPreference = 'Stop'
$repo = Join-Path $PSScriptRoot 'Include'
$key = Join-Path $env:USERPROFILE 'TurnHub-Private\TurnHub-keys\turnhub-firmware-signing-2026-10.pem'
$expectedKeyId = '0xc7, 0x6d, 0x7d, 0xdf'

function Fail($message) { Write-Host "`nERROR: $message" -ForegroundColor Red; exit 1 }

if (-not (Test-Path (Join-Path $repo 'tools\flash-all.ps1'))) { Fail "No TurnHub checkout at $repo." }
if (-not (Test-Path $key)) { Fail "Signing key not found at $key." }

# The boards must get firmware that trusts the new key, or later OTA updates fail.
$header = Get-Content -Raw (Join-Path $repo 'shared\include\firmware_signing_key.h')
if ($header -notmatch [regex]::Escape($expectedKeyId)) {
  $branch = git -C $repo branch --show-current
  Fail ("The checked-out firmware ($branch) still embeds the old public key. " +
        "Check out master after the key-rotation PR merges, or the branch claude/rotate-firmware-signing-key.")
}

$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path $python)) { Fail 'PlatformIO Python was not found. Install PlatformIO first.' }
# Windows PowerShell turns a native command's stderr into a terminating error
# under 'Stop', so the check and the install run under 'Continue'.
$ErrorActionPreference = 'Continue'
& $python -c 'import cryptography' 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
  Write-Host "Installing python 'cryptography' 46.0.7 into PlatformIO's Python (one time)." -ForegroundColor Cyan
  & $python -m pip install --disable-pip-version-check cryptography==46.0.7 2>&1 | ForEach-Object { "$_" }
  if ($LASTEXITCODE -ne 0) { Fail "Could not install 'cryptography'." }
}
$ErrorActionPreference = 'Stop'

$flashArgs = @('-Sign', '-Key', $key)
if ($NoPull) { $flashArgs += '-NoPull' }
if ($DryRun) { $flashArgs += '-DryRun' }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo 'tools\flash-all.ps1') @flashArgs
exit $LASTEXITCODE
