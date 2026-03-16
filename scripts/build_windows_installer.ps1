$ErrorActionPreference = "Stop"

$repoRoot   = (Resolve-Path ".").Path
$buildDir   = Join-Path $repoRoot "build"
$releaseDir = Join-Path $buildDir "Release"
$distDir    = Join-Path $repoRoot "dist"
$nsiScript  = Join-Path $repoRoot "deploy\windows\AMC_Installer.nsi"

# Resolve Qt root from install-qt-action env
if (-not $env:Qt6_DIR) {
    throw "Qt6_DIR environment variable not found."
}

# Qt6_DIR usually points to something like ...\msvc2022_64\lib\cmake\Qt6
$qtCmakeDir = $env:Qt6_DIR
$qtRoot = Resolve-Path (Join-Path $qtCmakeDir "..\..\..")
$qtRoot = $qtRoot.Path
$qtBin  = Join-Path $qtRoot "bin"
$qmlRoot = Join-Path $qtRoot "qml"

$exe = Join-Path $releaseDir "AMarinerControl.exe"
$windeployqt = Join-Path $qtBin "windeployqt.exe"
$makensis = "C:\Program Files (x86)\NSIS\makensis.exe"

if (!(Test-Path $exe)) {
    throw "Executable not found: $exe"
}
if (!(Test-Path $windeployqt)) {
    throw "windeployqt not found: $windeployqt"
}
if (!(Test-Path $makensis)) {
    throw "makensis not found: $makensis"
}
if (!(Test-Path $nsiScript)) {
    throw "NSIS script not found: $nsiScript"
}

New-Item -ItemType Directory -Force $distDir | Out-Null

# Clean prior deploy artifacts from Release
$deployDirs = @(
    "generic","geoservices","iconengines","imageformats","multimedia",
    "networkinformation","platforminputcontexts","platforms","position",
    "qml","qmltooling","sensors","sqldrivers","styles","texttospeech",
    "tls","translations"
)

foreach ($d in $deployDirs) {
    $p = Join-Path $releaseDir $d
    if (Test-Path $p) {
        Remove-Item $p -Recurse -Force
    }
}

Get-ChildItem $releaseDir -Filter "Qt6*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $releaseDir -Filter "av*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $releaseDir -Filter "sw*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $releaseDir -Filter "D3Dcompiler_47.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem $releaseDir -Filter "opengl32sw.dll" -ErrorAction SilentlyContinue | Remove-Item -Force

# Deploy Qt runtime + QML
& $windeployqt `
  --release `
  --force `
  --compiler-runtime `
  --qmldir (Join-Path $repoRoot "src") `
  $exe

if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

# Ensure QtQuick3D got copied
$qtQuick3DSrc = Join-Path $qmlRoot "QtQuick3D"
$qtQuick3DDst = Join-Path $releaseDir "qml\QtQuick3D"
$qtQuick3DPlugin = Join-Path $qtQuick3DDst "qquick3dplugin.dll"

if (!(Test-Path $qtQuick3DPlugin)) {
    if (!(Test-Path $qtQuick3DSrc)) {
        throw "QtQuick3D source module missing: $qtQuick3DSrc"
    }
    New-Item -ItemType Directory -Force $qtQuick3DDst | Out-Null
    Copy-Item (Join-Path $qtQuick3DSrc "*") $qtQuick3DDst -Recurse -Force
}

if (!(Test-Path $qtQuick3DPlugin)) {
    throw "QtQuick3D plugin still missing after manual copy."
}

# Write qt.conf
$qtConf = Join-Path $releaseDir "qt.conf"
@"
[Paths]
Plugins=plugins
Qml2Imports=qml
Imports=qml
"@ | Set-Content -Path $qtConf -Encoding ASCII

# Build installer
& $makensis /DAPPDIR="$releaseDir" /DOUTDIR="$distDir" $nsiScript
if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}

Write-Host "Installer build complete."