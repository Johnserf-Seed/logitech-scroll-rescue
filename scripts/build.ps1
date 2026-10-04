param([switch]$QA)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw '请安装 Visual Studio Build Tools 的 C++ 桌面开发组件。' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw '找不到 C++ 编译工具。' }
$compilerRoot = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name | Select-Object -Last 1
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\windows.h') } | Sort-Object Name | Select-Object -Last 1
if (-not $sdkVersion) { throw '找不到 Windows SDK。' }
$buildRoot = Join-Path $projectRoot $(if ($QA) { 'build\qa' } else { 'build\release' })
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$compiler = Join-Path $compilerRoot.FullName 'bin\Hostx64\x64\cl.exe'
$resourceCompiler = Join-Path $sdkRoot "bin\$($sdkVersion.Name)\x64\rc.exe"
$resourcePath = Join-Path $buildRoot 'app.res'
$arguments = @('/nologo','/utf-8','/O1','/GL','/MT','/W4','/WX','/GR-','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00',"/I$($compilerRoot.FullName)\include", "/I$($sdkVersion.FullName)\ucrt", "/I$($sdkVersion.FullName)\shared", "/I$($sdkVersion.FullName)\um", "/Fo$buildRoot\", "/Fe$buildRoot\scroll-rescue.exe")
if ($QA) { $arguments += '/DSCROLL_RESCUE_QA' }
$arguments += @((Join-Path $projectRoot 'src\main.cpp'),(Join-Path $projectRoot 'src\device.cpp'),(Join-Path $projectRoot 'src\cli.cpp'),(Join-Path $projectRoot 'src\gui.cpp'),(Join-Path $projectRoot 'src\locale.cpp'),$resourcePath,'/link','/SUBSYSTEM:WINDOWS','/LTCG','/OPT:REF','/OPT:ICF','/INCREMENTAL:NO','/DYNAMICBASE','/NXCOMPAT','/MANIFEST:EMBED',"/MANIFESTINPUT:$projectRoot\app.manifest", "/LIBPATH:$($compilerRoot.FullName)\lib\x64", "/LIBPATH:$sdkRoot\Lib\$($sdkVersion.Name)\ucrt\x64", "/LIBPATH:$sdkRoot\Lib\$($sdkVersion.Name)\um\x64",'user32.lib','gdi32.lib','shell32.lib','advapi32.lib','cfgmgr32.lib','dwmapi.lib','ole32.lib','kernel32.lib')
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $sdkRoot "bin\$($sdkVersion.Name)\x64") + ';' + (Split-Path -Parent $compiler) + ';' + $previousPath
    & $resourceCompiler /nologo "/I$projectRoot\src" "/I$projectRoot\assets" "/fo$resourcePath" (Join-Path $projectRoot 'assets\app.rc')
    if ($LASTEXITCODE -ne 0) { throw '图标资源编译失败。' }
    & $compiler @arguments
} finally {
    $env:PATH = $previousPath
}
if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
Get-Item -LiteralPath (Join-Path $buildRoot 'scroll-rescue.exe') | Select-Object FullName,Length
