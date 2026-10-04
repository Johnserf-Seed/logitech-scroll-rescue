param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CommandArguments)
$ErrorActionPreference = 'Stop'
$selectedLanguage = (Get-ItemProperty -LiteralPath 'HKCU:\Software\ScrollRescue' -Name Language -ErrorAction SilentlyContinue).Language
for ($argumentIndex = 0; $argumentIndex + 1 -lt $CommandArguments.Count; $argumentIndex++) {
    if ($CommandArguments[$argumentIndex] -ceq '--lang') { $selectedLanguage = $CommandArguments[$argumentIndex + 1]; break }
}
$useChinese = if ($selectedLanguage -in @('zh','zh-CN')) { $true } elseif ($selectedLanguage -in @('en','en-US')) { $false } else { (Get-UICulture).TwoLetterISOLanguageName -eq 'zh' }
$program = Join-Path $PSScriptRoot 'scroll-rescue.exe'
if (-not (Test-Path -LiteralPath $program)) { $program = Join-Path $PSScriptRoot 'build\release\scroll-rescue.exe' }
if (-not (Test-Path -LiteralPath $program)) {
    throw $(if ($useChinese) { '找不到 scroll-rescue.exe，请先解压完整便携包或构建项目。' } else { 'scroll-rescue.exe was not found. Extract the full portable package or build the project first.' })
}
if (-not $CommandArguments) { $CommandArguments = @('--help') }
$quotedArguments = foreach ($argument in $CommandArguments) {
    '"' + [regex]::Replace([regex]::Replace($argument, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $program
$startInfo.Arguments = $quotedArguments -join ' '
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
$startInfo.StandardErrorEncoding = [Text.Encoding]::UTF8
$process = [Diagnostics.Process]::new()
$process.StartInfo = $startInfo
try {
    if (-not $process.Start()) { throw $(if ($useChinese) { '无法启动程序。' } else { 'Unable to start the program.' }) }
    $errorTask = $process.StandardError.ReadToEndAsync()
    $output = $process.StandardOutput.ReadToEnd()
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    if ($output) { Write-Output ($output.TrimEnd([char[]]"`r`n")) }
    $errorOutput = $errorTask.Result
    if ($errorOutput) { [Console]::Error.Write($errorOutput) }
} finally {
    $process.Dispose()
}
exit $exitCode
