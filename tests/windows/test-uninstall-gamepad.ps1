# Mock every CIM boundary; never query or change an installed driver.
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$scriptPath=Join-Path $repo 'src_assets\windows\misc\gamepad\uninstall-gamepad.ps1'
function Get-CimInstance { param($ClassName,$Filter) if($global:case -eq 'query-failure'){throw 'Injected query failure'};if($global:case -ne 'absent'){[pscustomobject]@{Name='fixture'}} }
function Invoke-CimMethod { param($InputObject,$MethodName) $global:called=$true;if($global:case -eq 'method-failure'){throw 'Injected method failure'};if($global:case -eq 'missing-status'){return @{}};return @{ReturnValue=[int]$global:case} }
foreach($case in @('absent','0','3010','1603','query-failure','method-failure','missing-status')) {
    $global:case=$case;$global:called=$false
    & $scriptPath
    $expected=switch($case){'absent'{0} '0'{0} '3010'{3010} '1603'{1603} default{1}}
    if($LASTEXITCODE -ne $expected){throw "$case returned $LASTEXITCODE expected $expected"}
    if(($case -in @('absent','query-failure')) -and $global:called){throw 'Uninstall called unexpectedly'}
    Write-Host "PASS gamepad removal $case -> $LASTEXITCODE"
}
