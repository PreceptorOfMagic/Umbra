# Real harmless processes in temporary install trees; no host, driver or service runs.
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$scriptPath=Join-Path $repo 'src_assets\windows\misc\service\stop-owned-processes.ps1'
$temp=Join-Path ([IO.Path]::GetTempPath()) ('Umbra Process Fixture '+[Guid]::NewGuid().ToString('N'))
$install=Join-Path $temp 'Install With Spaces'
$foreign=Join-Path $temp 'Another Install'
$processes=New-Object 'Collections.Generic.List[Diagnostics.Process]'
try {
    New-Item -ItemType Directory -Force -Path (Join-Path $install 'scripts'),(Join-Path $install 'tools'),$foreign | Out-Null
    $script=Join-Path $install 'scripts\stop-owned-processes.ps1'
    # Use unique fixture names so a protected real service cannot block this isolated test.
    # Only the fixed executable basenames are substituted; production control flow is unchanged.
    $prefix='umbra-fixture-'+[Guid]::NewGuid().ToString('N').Substring(0,12)+'-'
    $source=[IO.File]::ReadAllText($scriptPath)
    $aliases=@{}
    foreach($leaf in @('sunshine.exe','aurora-link.exe','sunshinesvc.exe','dxgi-info.exe','audio-info.exe','dispwake.exe')) {
        if(!$source.Contains($leaf)){throw "Production allowlist no longer contains $leaf"}
        $aliases[$leaf]=$prefix+$leaf
        $source=$source.Replace($leaf,$aliases[$leaf])
    }
    $restored=$source
    foreach($leaf in $aliases.Keys){$restored=$restored.Replace($aliases[$leaf],$leaf)}
    if($restored -cne [IO.File]::ReadAllText($scriptPath)){throw 'Fixture transform changed production control flow'}
    [IO.File]::WriteAllText($script,$source)
    $exe=Join-Path $temp 'benign.exe'
    Add-Type -OutputAssembly $exe -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
class Benign {
    static void Main(string[] args) {
        if (args.Length > 0 && args[0] == "helper") {
            using (var stop=new EventWaitHandle(false, EventResetMode.ManualReset, "Local\\EclipseLinkStop-"+Process.GetCurrentProcess().Id)) {
                File.WriteAllText(args[1]+".ready", "ready");
                if (stop.WaitOne(60000)) File.WriteAllText(args[1], "cleaned");
            }
        } else {
            File.WriteAllText(args[1]+".ready", "ready");
            Thread.Sleep(60000);
        }
    }
}
'@
    $owned=New-Object 'Collections.Generic.List[Diagnostics.Process]'
    $marker=Join-Path $temp 'helper-cleanup'
    $i=0
    foreach($relative in @('sunshine.exe','aurora-link.exe','tools\sunshinesvc.exe','tools\dxgi-info.exe','tools\audio-info.exe','dispwake.exe','tools\dispwake.exe')) {
        $aliased=$relative.Replace([IO.Path]::GetFileName($relative),$aliases[[IO.Path]::GetFileName($relative)])
        $path=Join-Path $install $aliased;Copy-Item $exe $path
        $mode=if($relative -eq 'aurora-link.exe'){'helper'}else{'sleep'}
        $ready=if($mode -eq 'helper'){$marker}else{Join-Path $temp ('owned'+$i)}
        $p=Start-Process -FilePath $path -ArgumentList ($mode+' "'+$ready+'"') -PassThru -WindowStyle Hidden
        $processes.Add($p);$owned.Add($p);$i++
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        while(!(Test-Path -LiteralPath ($ready+'.ready'))) { if([DateTime]::UtcNow -gt $deadline){throw 'Fixture startup timeout'};Start-Sleep -Milliseconds 20 }
    }
    $unrelated=New-Object 'Collections.Generic.List[Diagnostics.Process]'
    foreach($leaf in @('sunshine.exe','aurora-link.exe','sunshinesvc.exe','dxgi-info.exe','audio-info.exe','dispwake.exe')) {
        $path=Join-Path $foreign $aliases[$leaf];Copy-Item $exe $path
        $ready=Join-Path $temp ('foreign'+$leaf)
        $p=Start-Process -FilePath $path -ArgumentList ('sleep "'+$ready+'"') -PassThru -WindowStyle Hidden
        $processes.Add($p);$unrelated.Add($p)
    }
    $watch=[Diagnostics.Stopwatch]::StartNew()
    & $script
    if($LASTEXITCODE -ne 0){throw "Cleanup failed: $LASTEXITCODE"}
    if($watch.Elapsed.TotalSeconds -gt 20){throw 'Cleanup exceeded bounded time'}
    foreach($p in $owned){if(!$p.WaitForExit(1000)){throw "Owned PID $($p.Id) remains"}}
    if(!(Test-Path -LiteralPath $marker)){throw 'Helper did not run cooperative cleanup'}
    if($owned[1].ExitCode -ne 0){throw 'Cooperative helper was forcibly terminated'}
    foreach($p in $unrelated){if($p.HasExited){throw "Unrelated PID $($p.Id) was stopped"}}
    Write-Host "PASS exact-path ownership, all seven owned executables stopped; six unrelated same-name processes preserved; helper cleanup marker and exit 0 ($($watch.ElapsedMilliseconds) ms)."
    & $script
    if($LASTEXITCODE -ne 0){throw 'Idempotent cleanup failed'}
    # A legacy helper without an event must take the bounded forced fallback.
    $legacy=Start-Process -FilePath (Join-Path $install $aliases['aurora-link.exe']) -ArgumentList ('sleep "'+(Join-Path $temp 'legacy')+'"') -PassThru -WindowStyle Hidden
    $processes.Add($legacy)
    Start-Sleep -Milliseconds 100
    $watch.Restart(); & $script
    if($LASTEXITCODE -ne 0 -or !$legacy.WaitForExit(1000) -or $legacy.ExitCode -ne 1){throw 'Legacy helper fallback failed'}
    if($watch.ElapsedMilliseconds -lt 4500 -or $watch.Elapsed.TotalSeconds -gt 15){throw 'Helper grace deadline not respected'}
    Write-Host "PASS legacy helper five-second grace then forced fallback ($($watch.ElapsedMilliseconds) ms); idempotent second cleanup."
} finally {
    foreach($p in $processes){try{if(!$p.HasExited){$p.Kill();$p.WaitForExit(5000)|Out-Null}}finally{$p.Dispose()}}
    if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp -Recurse -Force}
}
