# Builds a Release, runs the tests, and packs Oxide-3216.exe with the README into dist\Oxide-3216-<version>-win64.zip.
# Usage:  powershell -ExecutionPolicy Bypass -File release.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

& powershell -ExecutionPolicy Bypass -File build.ps1 -Config Release
if ($LASTEXITCODE) { throw "Build or tests failed." }

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(Oxide3216 VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$version += (Select-String -Path CMakeLists.txt -Pattern 'OXIDE_VERSION_LABEL "\$\{PROJECT_VERSION\}([^"]*)"').Matches[0].Groups[1].Value
$stage = "dist\Oxide-3216-$version"
$zip = "dist\Oxide-3216-$version-win64.zip"

# A copy running from the dist folder can't be replaced: leave dist alone rather than touch it.
$stageExe = Join-Path $PSScriptRoot "$stage\Oxide-3216.exe"
if (Get-Process Oxide-3216 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $stageExe }) {
    Write-Warning "Oxide-3216 is running from $stage - built and tested, but dist was NOT refreshed. Close it and run release.ps1 again."
    exit 0
}

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "build\Oxide3216_artefacts\Release\Oxide-3216.exe" $stage
Copy-Item "README.txt" $stage

Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "`nRelease: $zip"
