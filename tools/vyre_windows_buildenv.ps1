$ErrorActionPreference = "Stop"

& "$PSScriptRoot\windows_buildenv.bat" setup
if ($LASTEXITCODE -ne 0) {
    throw "Mixxx Windows build environment setup failed with exit code $LASTEXITCODE."
}

$buildEnvironment = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot "..\buildenv") -Directory |
    Where-Object Name -Like "mixxx-deps-2.5-x64-windows-*" |
    Select-Object -First 1
if ($null -eq $buildEnvironment) {
    throw "The Mixxx 2.5 Windows build environment was not found."
}

$qtHeader = Join-Path $buildEnvironment.FullName "installed\x64-windows\include\Qt6\QtCore\qcompilerdetection.h"
$content = Get-Content -LiteralPath $qtHeader -Raw
$oldDefinition = @'
#  define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) stdext::make_unchecked_array_iterator(x) // Since _MSC_VER >= 1800
#  define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) stdext::make_checked_array_iterator(x, size_t(N)) // Since _MSC_VER >= 1500
'@
$newDefinition = @'
#  if _MSC_VER >= 1950
#    define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) (x)
#    define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) (x)
#  else
#    define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) stdext::make_unchecked_array_iterator(x) // Since _MSC_VER >= 1800
#    define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) stdext::make_checked_array_iterator(x, size_t(N)) // Since _MSC_VER >= 1500
#  endif
'@

if ($content.Contains($oldDefinition)) {
    Set-Content -LiteralPath $qtHeader -Value $content.Replace($oldDefinition, $newDefinition) -NoNewline
    Write-Host "Applied the Visual Studio 2026 compatibility patch to Qt 6.5."
} elseif (-not $content.Contains("#  if _MSC_VER >= 1950")) {
    throw "The expected Qt checked-array iterator definitions were not found."
} else {
    Write-Host "The Visual Studio 2026 compatibility patch is already applied."
}
