# Builds a Release, runs the tests, and packs Oxide-3216.exe with the README into dist\Oxide-3216-<version>-win64.zip.
# Usage:  powershell -ExecutionPolicy Bypass -File release.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

& powershell -ExecutionPolicy Bypass -File build.ps1 -Config Release
if ($LASTEXITCODE) { throw "Build or tests failed." }

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(Oxide3216 VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$stage = "dist\Oxide-3216-$version"
$zip = "dist\Oxide-3216-$version-win64.zip"

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "build\Oxide3216_artefacts\Release\Oxide-3216.exe" $stage
Copy-Item "README.txt" $stage

Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "`nRelease: $zip"
