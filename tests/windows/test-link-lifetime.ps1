param([Parameter(Mandatory=$true)][string]$Fixture)
$ErrorActionPreference = 'Stop'
$stage = Join-Path ([IO.Path]::GetTempPath()) ('umbra-link-lifetime-' + [guid]::NewGuid())
New-Item -ItemType Directory $stage | Out-Null
$hostProcess = $null
$worker = $null
try {
    $link = Join-Path $stage 'aurora-link.exe'
    Copy-Item $Fixture $link
    Add-Type -OutputAssembly (Join-Path $stage 'sunshine.exe') -OutputType ConsoleApplication -TypeDefinition @'
public class HostFixture { public static void Main() { System.Threading.Thread.Sleep(-1); } }
'@
    # Same process name in another install must not be adopted.
    $other = Join-Path $stage 'other'
    New-Item -ItemType Directory $other | Out-Null
    Copy-Item (Join-Path $stage 'sunshine.exe') $other
    $hostProcess = Start-Process (Join-Path $other 'sunshine.exe') -PassThru -WindowStyle Hidden
    $worker = Start-Process $link -ArgumentList run -PassThru -WindowStyle Hidden
    if (-not $worker.WaitForExit(3000) -or $worker.ExitCode -ne 1) { throw 'Orphan helper did not refuse another install host' }
    $hostProcess.Kill(); $hostProcess.WaitForExit(); $hostProcess = $null
    Write-Host 'PASS orphan helper refuses unrelated installation'
    # A same-session duplicate host is ambiguous and must be refused.
    $hostProcess = Start-Process (Join-Path $stage 'sunshine.exe') -PassThru -WindowStyle Hidden
    $duplicate = Start-Process (Join-Path $stage 'sunshine.exe') -PassThru -WindowStyle Hidden
    try {
        $worker = Start-Process $link -ArgumentList run -PassThru -WindowStyle Hidden
        if (-not $worker.WaitForExit(3000) -or $worker.ExitCode -ne 1) { throw 'Ambiguous host was adopted' }
    } finally { $duplicate.Kill(); $duplicate.WaitForExit(); $hostProcess.Kill(); $hostProcess.WaitForExit(); $hostProcess = $null }
    Write-Host 'PASS ambiguous same-session hosts refused'
    foreach ($verb in @('run', 'mic')) {
        $hostProcess = Start-Process (Join-Path $stage 'sunshine.exe') -PassThru -WindowStyle Hidden
        $worker = Start-Process $link -ArgumentList $verb -PassThru -WindowStyle Hidden
        Start-Sleep -Milliseconds 350
        if ($worker.HasExited) { throw "$verb fixture exited while its host was alive" }
        $hostProcess.Kill(); $hostProcess.WaitForExit(); $hostProcess = $null
        if (-not $worker.WaitForExit(3000) -or $worker.ExitCode -ne 0) { throw "$verb survived its host" }
        Write-Host "PASS $verb lifetime ends with owned host"
    }
    $hostProcess = Start-Process (Join-Path $stage 'sunshine.exe') -PassThru -WindowStyle Hidden
    $worker = Start-Process $link -ArgumentList run -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 350
    $stop = [Threading.EventWaitHandle]::OpenExisting("Local\EclipseLinkStop-$($worker.Id)")
    try { $stop.Set() | Out-Null } finally { $stop.Dispose() }
    if (-not $worker.WaitForExit(3000) -or $worker.ExitCode -ne 0) { throw 'Named session stop did not shut helper down cooperatively' }
    if ($hostProcess.HasExited) { throw 'Stopping helper terminated its host' }
    Write-Host 'PASS named session-stop event cleans helper and retains host'
} finally {
    foreach ($process in @($worker, $hostProcess)) {
        if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
    Remove-Item $stage -Recurse -Force
}
