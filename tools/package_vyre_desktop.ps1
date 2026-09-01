$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$developerShell = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$buildRoot = Join-Path $repositoryRoot "build\vyre-release"
$desktopPath = [Environment]::GetFolderPath("Desktop")
$packagePath = Join-Path $desktopPath "VYRE DJ"
$executable = Join-Path $packagePath "VYRE DJ.exe"
$shortcutPath = Join-Path $desktopPath "VYRE DJ.lnk"

& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
New-Item -ItemType Directory -Path $packagePath -Force | Out-Null
& $cmake --install $buildRoot --config RelWithDebInfo --prefix $packagePath
if ($LASTEXITCODE -ne 0) {
    throw "Packaging VYRE DJ failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $executable)) {
    throw "The packaged executable was not produced at $executable."
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $executable
$shortcut.WorkingDirectory = $packagePath
$shortcut.IconLocation = "$executable,0"
$shortcut.Description = "Launch VYRE DJ"
$shortcut.Save()

Write-Host "Packaged $executable"
Write-Host "Created $shortcutPath"
