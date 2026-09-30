# Builds WSDDM with Qt/CMake and packs the deployed tree into a release zip.
# Output: dist\wsddm-win-x64.zip (attach the zip to a GitHub release).
#
# The build is MinGW/Qt, not a single-file .NET exe: Qt Quick needs its runtime DLLs
# and plugins, so the zip contains a directory rather than a lone executable. The same
# single-file trick that worked for the C# build is not available here.

param(
    [string]$QtPath = 'C:\Qt\6.8.3\mingw_64',
    [string]$MingwPath = 'C:\Qt\Tools\mingw1310_64\bin'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
$staging = Join-Path $dist 'wsddm'

Write-Host '[wsddm] configuring (Qt + Ninja)' -ForegroundColor Cyan

# The MinGW that ships with Qt is pinned deliberately: Qt's own libraries are built with
# it, and a newer GCC on PATH would mix ABIs.
if (-not (Test-Path $MingwPath)) { throw "MinGW not found at $MingwPath" }
$env:Path = "$MingwPath;$QtPath\bin;$env:USERPROFILE\scoop\shims;$env:Path"

$scoopShims = "$env:USERPROFILE\scoop\shims"
$cmake = Join-Path $scoopShims 'cmake.exe'
$ninja = Join-Path $scoopShims 'ninja.exe'
foreach ($tool in @($cmake, $ninja)) {
    if (-not (Test-Path $tool)) { throw "$([System.IO.Path]::GetFileName($tool)) not found (scoop install cmake ninja)" }
}

& $cmake -S $root -B $build -G Ninja `
    -DCMAKE_PREFIX_PATH="$($QtPath -replace '\\','/')" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

Write-Host '[wsddm] building' -ForegroundColor Cyan
& $cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }

$exe = Join-Path $build 'wsddm.exe'
if (-not (Test-Path $exe)) { throw 'build did not produce wsddm.exe' }

Write-Host '[wsddm] staging with windeployqt' -ForegroundColor Cyan
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Copy-Item $exe (Join-Path $staging 'wsddm.exe') -Force
# theme.conf sits beside the exe so it can be edited without a rebuild.
Copy-Item (Join-Path $root 'theme.conf') (Join-Path $staging 'theme.conf') -Force

$windeployqt = Join-Path $QtPath 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) { throw "windeployqt not found at $windeployqt" }
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    (Join-Path $staging 'wsddm.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

$zip = Join-Path $dist 'wsddm-win-x64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $staging -DestinationPath $zip -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item $zip).Length
Write-Host "[wsddm] built $zip" -ForegroundColor Green
Write-Host "[wsddm] $size bytes" -ForegroundColor Green
Write-Host "[wsddm] sha256: $hash" -ForegroundColor Green
Write-Host '[wsddm] paste this hash into wsddm.json (url + autoupdate.hash) before publishing the release' -ForegroundColor Yellow
