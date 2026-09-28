# Builds WSDDM as a single-file exe and packs it into a release zip.
# Output: dist\wsddm.exe and dist\wsddm-win-x64.zip (attach the zip to a GitHub release).

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSCommandPath
$dist = Join-Path $root 'dist'
$staging = Join-Path $dist 'wsddm'

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Write-Host '[wsddm] publishing (self-contained, single file)' -ForegroundColor Cyan

dotnet publish (Join-Path $root 'src\Wsddm\Wsddm.csproj') `
    -c Release -r win-x64 --self-contained `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -o $staging

$exe = Join-Path $staging 'wsddm.exe'
if (-not (Test-Path $exe)) { throw 'publish did not produce wsddm.exe' }
Copy-Item $exe (Join-Path $dist 'wsddm.exe') -Force

$zip = Join-Path $dist 'wsddm-win-x64.zip'
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Write-Host "[wsddm] built $zip" -ForegroundColor Green
Write-Host "[wsddm] sha256: $hash" -ForegroundColor Green
Write-Host '[wsddm] paste this hash into wsddm.json (url + autoupdate.hash) before publishing the release' -ForegroundColor Yellow
