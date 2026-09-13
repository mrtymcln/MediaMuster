# Run from the repository root after building Release.
$ErrorActionPreference = 'Stop'
if (-not $env:APP_VERSION) { throw 'Set APP_VERSION to the handwritten app version' }

cd build
New-Item -ItemType Directory -Path dist -Force | Out-Null
Copy-Item -Path Release\MediaMuster.exe -Destination dist\
Copy-Item -Recurse -Path Release\file-operation-samples -Destination dist\

windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler dist\MediaMuster.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

### Leftovers from custom icons ###
$strip = @('Qt6Network.dll', 'Qt6Svg.dll', 'tls', 'networkinformation',
           'generic', 'iconengines', 'imageformats')
foreach ($item in $strip) {
  Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "dist\$item"
  if (Test-Path "dist\$item") { throw "$item survived the strip" }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -property installationPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not $vs) { throw "Visual Studio installation was not found" }
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory | Select-Object -Last 1
if (-not $crt) { throw "VC++ CRT redist folder not found under $vs" }
$keep = @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
          'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll',
          'vcruntime140.dll', 'vcruntime140_1.dll')
foreach ($dll in $keep) {
  $src = Join-Path $crt.FullName $dll
  if (-not (Test-Path $src)) { throw "Expected CRT DLL missing from redist: $dll" }
  Copy-Item $src dist\
}

Rename-Item dist "MediaMuster-${env:APP_VERSION}-Win"
