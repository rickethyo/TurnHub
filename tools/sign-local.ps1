<#
.SYNOPSIS
  Builds firmware from this working copy and signs it with the owner's local
  key, producing .thfw packages you can install without a GitHub release: upload
  them on the Atlas portal's /update page (Atlas), its Sigil firmware page
  (Sigils), or from the app. The boards accept them like release packages.

.DESCRIPTION
  The key is read from ..\Private\TurnHub-keys\ (outside the git repo) and is
  never copied, printed or put in an environment variable. Packages are
  verified against the public key in shared/include/firmware_signing_key.h
  before they are reported as ready.

  OTA only accepts the same or a newer version: raise PATCH in the project's
  firmware_version.h when you want devices to accept an update over a build
  they already run, or reinstall the same version freely. Going to an older
  version needs a USB flash (SIGIL_OTA.md).

.PARAMETER Products  atlas, sigil-eink, sigil-oled. Default: all three.
.PARAMETER Key       Path to the PEM key. Default: the single .pem in ..\Private\TurnHub-keys.
.PARAMETER OutDir    Where packages go. Default: ..\Private\TurnHub-builds\local-<time>-<commit>.
#>
param(
  [string[]]$Products = @('atlas', 'sigil-eink', 'sigil-oled'),
  [string]$Key,
  [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$private = Join-Path (Split-Path -Parent $root) 'Private'
$thfw = Join-Path $root 'tools\firmware\thfw.py'

function Fail($message) { Write-Host "`nERROR: $message" -ForegroundColor Red; exit 1 }

# `powershell -File ... -Products a,b` delivers one string "a,b": split it.
$Products = @($Products | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($p in $Products) {
  if ($p -notin 'atlas', 'sigil-eink', 'sigil-oled') { Fail "Unknown product '$p' (atlas, sigil-eink, sigil-oled)." }
}

$map = @{
  'atlas'      = @{ Dir = 'Atlas'; Env = 'atlas' }
  'sigil-eink' = @{ Dir = 'Sigil'; Env = 'sigil' }
  'sigil-oled' = @{ Dir = 'Sigil'; Env = 'sigil-oled' }
}

$pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
if (-not $pio) { $pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe' }
if (-not (Test-Path $pio)) { Fail 'PlatformIO (pio) was not found.' }
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path $python)) { $python = 'python' }
& $python -c 'import cryptography' 2>$null
if ($LASTEXITCODE -ne 0) { Fail "python 'cryptography' is missing: python -m pip install cryptography==46.0.7" }

if (-not $Key) {
  $found = @(Get-ChildItem (Join-Path $private 'TurnHub-keys') -Filter *.pem -ErrorAction SilentlyContinue)
  if ($found.Count -ne 1) { Fail "Expected exactly one .pem in $private\TurnHub-keys (found $($found.Count)); pass -Key." }
  $Key = $found[0].FullName
}
if (-not (Test-Path $Key)) { Fail "Key not found: $Key" }

Set-Location $root
$sha = (git rev-parse --short=12 HEAD)
$dirty = [bool](git status --porcelain)
# The package build id is hex, 16 digits at most: the commit alone when clean,
# else 8 digits of the commit and 8 of the uncommitted changes, so every
# distinct local build is told apart.
$buildId = $sha
if ($dirty) {
  $diffHash = (git diff HEAD | git hash-object --stdin)
  $buildId = $sha.Substring(0, 8) + $diffHash.Substring(0, 8)
}
if (-not $OutDir) {
  $OutDir = Join-Path $private ("TurnHub-builds\local-{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $sha)
}
New-Item -ItemType Directory -Force $OutDir | Out-Null
Write-Host "Signing with $(Split-Path -Leaf $Key); build id $buildId" -ForegroundColor Cyan
if ($dirty) { Write-Host 'Working copy has uncommitted changes; the build id marks them.' -ForegroundColor Yellow }

$results = @()
foreach ($product in $Products) {
  $t = $map[$product]
  Write-Host "`n== Building $product" -ForegroundColor Cyan
  & $pio run --project-dir (Join-Path $root $t.Dir) --environment $t.Env
  if ($LASTEXITCODE -ne 0) { $results += "$product : BUILD FAILED"; continue }
  $bin = Join-Path $root "$($t.Dir)\.pio\build\$($t.Env)\firmware.bin"
  $tmp = Join-Path $OutDir "$product.thfw"
  & $python $thfw package $bin --out $tmp --key $Key --expect-product $product --build-id $buildId
  if ($LASTEXITCODE -ne 0) { $results += "$product : SIGNING FAILED"; continue }
  & $python $thfw verify $tmp
  if ($LASTEXITCODE -ne 0) { $results += "$product : VERIFY FAILED"; continue }
  $version = (& $python $thfw info $tmp | ConvertFrom-Json).version -join '.'
  $final = Join-Path $OutDir "$product-$version.thfw"
  Move-Item -Force $tmp $final
  $results += "$product : ready -> $final"
}

Write-Host "`n== Summary" -ForegroundColor Cyan
$bad = 0
foreach ($r in $results) {
  if ($r -match 'ready') { Write-Host "  $r" -ForegroundColor Green } else { Write-Host "  $r" -ForegroundColor Red; $bad++ }
}
exit $(if ($bad) { 1 } else { 0 })
