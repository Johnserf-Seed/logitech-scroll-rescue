param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    if (-not $SkipBuild) {
        & cargo build --release --locked
        if ($LASTEXITCODE -ne 0) { throw '构建失败。' }
    }
    $packagePath = Join-Path $projectRoot 'dist\scroll-rescue-windows-x64'
    New-Item -ItemType Directory -Path $packagePath -Force | Out-Null
    Copy-Item -LiteralPath 'target\release\scroll-rescue.exe','target\release\scroll-rescue-cli.exe','reset.bat','README.md','LICENSE' -Destination $packagePath -Force
    New-Item -ItemType Directory -Path (Join-Path $packagePath 'legacy') -Force | Out-Null
    Copy-Item -LiteralPath 'legacy\reset-original.bat' -Destination (Join-Path $packagePath 'legacy') -Force
    $archive = Join-Path $projectRoot 'dist\scroll-rescue-windows-x64.zip'
    Compress-Archive -LiteralPath $packagePath -DestinationPath $archive -Force
    Write-Output $archive
} finally {
    Pop-Location
}
