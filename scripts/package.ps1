param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') }
$packagePath = Join-Path $projectRoot 'dist\scroll-rescue-cpp-windows-x64'
New-Item -ItemType Directory -Path $packagePath -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\release\scroll-rescue.exe') -Destination $packagePath -Force
foreach ($packageFile in @('scroll-rescue-cli.ps1','scroll-rescue-cli.cmd','reset.bat','README.md','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $packageFile) -Destination $packagePath -Force
}
$legacyPath = Join-Path $packagePath 'legacy'
New-Item -ItemType Directory -Path $legacyPath -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'legacy\reset-original.bat') -Destination $legacyPath -Force
$archive = Join-Path $projectRoot 'dist\scroll-rescue-cpp-windows-x64.zip'
Compress-Archive -LiteralPath $packagePath -DestinationPath $archive -Force
Get-Item -LiteralPath $archive | Select-Object FullName,Length
