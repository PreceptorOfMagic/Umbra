# Inject a fake SCM at the script boundary; no actual Windows service is changed.
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.ServiceProcess
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$scriptPath=Join-Path $repo 'src_assets\windows\misc\service\service-control.ps1'
$expected='C:\Fixture With Spaces\tools\sunshinesvc.exe'
function Get-CimInstance { param($ClassName,$Filter) if(!$global:absent -and !$global:removed){return $global:fakeService} }
function Get-Service { param($Name) return $global:fakeController }
function Stop-Service { param($InputObject) $global:calls.Add('stop');if($global:failStop){throw 'Injected stop failure'} }
function Invoke-CimMethod { param($InputObject,$MethodName) $global:calls.Add('delete');if($global:failDelete){return @{ReturnValue=5}};$global:removed=$true;return @{ReturnValue=0} }
foreach($test in @('absent','foreign-legacy','foreign-current','inspect','stop','remove','stop-failure','wait-failure','delete-failure')) {
    $global:absent=$test -eq 'absent';$global:removed=$false
    $global:failStop=$test -eq 'stop-failure';$global:failWait=$test -eq 'wait-failure';$global:failDelete=$test -eq 'delete-failure'
    $global:calls=New-Object 'Collections.Generic.List[string]'
    $path=if($test -like 'foreign-*'){'"C:\Another Install\tools\sunshinesvc.exe"'}else{'"'+$expected+'"'}
    $global:fakeService=[pscustomobject]@{PathName=$path}
    $global:fakeController=[pscustomobject]@{Status=[ServiceProcess.ServiceControllerStatus]::Running}
    $global:fakeController | Add-Member ScriptMethod WaitForStatus { param($status,$timeout) $global:calls.Add('wait');if($global:failWait){throw 'Injected timeout'} }
    $global:fakeController | Add-Member ScriptMethod Dispose { $global:calls.Add('dispose') }
    $name=if($test -eq 'foreign-legacy'){'SunshineSvc'}else{'ApolloService'}
    $action=if($test -eq 'inspect'){'Inspect'}elseif($test -eq 'stop'){'Stop'}else{'Remove'}
    & $scriptPath -Name $name -Action $action -ExpectedPath $expected
    $wanted=if($test -in @('foreign-current','stop-failure','wait-failure','delete-failure')){1}else{0}
    if($LASTEXITCODE -ne $wanted){throw "$test returned $LASTEXITCODE, expected $wanted"}
    $actual=$global:calls -join ','
    $wantedCalls=switch($test){'stop'{'stop,wait,dispose'} 'remove'{'stop,wait,dispose,delete'} 'stop-failure'{'stop,dispose'} 'wait-failure'{'stop,wait,dispose'} 'delete-failure'{'stop,wait,dispose,delete'} default{''}}
    if($actual -ne $wantedCalls){throw "$test made unsafe/unexpected calls: $actual"}
    Write-Host "PASS service guard $test ($actual)"
}
