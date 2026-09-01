$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$developerShell = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$buildRoot = Join-Path $repositoryRoot "build\vyre-release"

if (-not (Test-Path -LiteralPath $developerShell)) {
    throw "The Visual Studio 2026 developer shell was not found at $developerShell."
}
if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake was not found at $cmake."
}

& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
& $cmake --build $buildRoot --config RelWithDebInfo --target mixxx --parallel 8
if ($LASTEXITCODE -ne 0) {
    throw "The VYRE DJ build failed with exit code $LASTEXITCODE."
}

$executable = Join-Path $buildRoot "RelWithDebInfo\VYRE DJ.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "The expected VYRE DJ executable was not produced at $executable."
}

Write-Host "Built $executable"
