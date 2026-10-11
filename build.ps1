# Builds Oxide-3216.exe and runs the engine tests.
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1 [-Config Release|Debug]
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path $cmake)) { throw "CMake not found. Install Visual Studio Build Tools with the C++ workload." }

# A copy running from the build folder locks the .exe and makes the link step fail. Never close or kill
# it (that loses unsaved work): stop and ask instead. Copies run from anywhere else (e.g. dist) are fine.
$buildExe = Join-Path $PSScriptRoot "build\Oxide3216_artefacts\$Config\Oxide-3216.exe"
$blocking = Get-Process Oxide-3216, ConsoleTape -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $buildExe }
if ($blocking) { throw "Oxide-3216 is running from the build folder ($buildExe). Close it yourself, then build again." }

& $cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build build --config $Config --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& "build\$Config\EngineTests.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }

Write-Host "`nBuilt: build\Oxide3216_artefacts\$Config\Oxide-3216.exe"
