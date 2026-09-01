$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $repositoryRoot "build\vyre-release"
$executable = Join-Path $buildRoot "RelWithDebInfo\VYRE DJ.exe"
$settingsPath = Join-Path $env:LOCALAPPDATA "VYRE DJ"
$resourcePath = Join-Path $repositoryRoot "res"

if (-not (Test-Path -LiteralPath $executable)) {
    throw "VYRE DJ has not been built at $executable."
}

New-Item -ItemType Directory -Path $settingsPath -Force | Out-Null
$env:QT_PLUGIN_PATH = $buildRoot
$env:QML2_IMPORT_PATH = (Join-Path $buildRoot "Qt6\qml") + ";" + (Join-Path $buildRoot "qml")

& $executable --settings-path $settingsPath --resource-path $resourcePath
