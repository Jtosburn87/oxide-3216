# Builds a Release, runs the tests, and packs ConsoleTape.exe with the README into dist\ConsoleTape-<version>-win64.zip.
# Usage:  powershell -ExecutionPolicy Bypass -File release.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

& powershell -ExecutionPolicy Bypass -File build.ps1 -Config Release
if ($LASTEXITCODE) { throw "Build or tests failed." }

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(ConsoleTape VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$stage = "dist\ConsoleTape-$version"
$zip = "dist\ConsoleTape-$version-win64.zip"

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "build\ConsoleTape_artefacts\Release\ConsoleTape.exe" $stage
Copy-Item "README.txt" $stage

Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "`nRelease: $zip"
