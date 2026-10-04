param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') }
$distRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$packagePath = [IO.Path]::GetFullPath((Join-Path $distRoot 'scroll-rescue-cpp-windows-x64'))
if (-not $packagePath.StartsWith($distRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '打包目录超出项目输出目录。'
}
if (Test-Path -LiteralPath $packagePath) {
    if ((Get-Item -LiteralPath $packagePath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw '打包目录不能是链接目录。'
    }
    Remove-Item -LiteralPath $packagePath -Recurse -Force
}
New-Item -ItemType Directory -Path $packagePath -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\release\scroll-rescue.exe') -Destination $packagePath -Force
foreach ($packageFile in @('scroll-rescue-cli.ps1','scroll-rescue-cli.cmd','README.md','README.en.md','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $packageFile) -Destination $packagePath -Force
}
$imagePath = Join-Path $packagePath 'docs\images'
New-Item -ItemType Directory -Path $imagePath -Force | Out-Null
foreach ($imageFile in @('screenshot-zh-CN.png','screenshot-en.png')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot ('docs\images\' + $imageFile)) -Destination $imagePath -Force
}
$archive = Join-Path $projectRoot 'dist\scroll-rescue-cpp-windows-x64.zip'
Compress-Archive -LiteralPath $packagePath -DestinationPath $archive -Force
Get-Item -LiteralPath $archive | Select-Object FullName,Length
