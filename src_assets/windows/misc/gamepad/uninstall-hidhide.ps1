# Removes HidHide only when Umbra setup installed it (install-hidhide.ps1 left a marker); a HidHide the user
# installed before Umbra stays.
$ErrorActionPreference = 'Stop'
$marker = Join-Path (Join-Path $env:ProgramData 'Umbra') 'hidhide-installed-by-umbra'
if (-not (Test-Path $marker)) {
    Write-Information 'HidHide was not installed by Umbra; leaving it.'
    exit 0
}
try {
    $result = 0
    foreach ($product in @(Get-CimInstance -ClassName Win32_Product -Filter "Name='HidHide'")) {
        $uninstall = Invoke-CimMethod -InputObject $product -MethodName Uninstall
        if ($null -eq $uninstall.ReturnValue) { throw 'HidHide removal returned no status' }
        $code = [int]$uninstall.ReturnValue
        if ($code -ne 0 -and $code -ne 3010) { exit $code }
        if ($code -eq 3010) { $result = 3010 }
    }
    Remove-Item -Force $marker
    exit $result
} catch {
    Write-Host "HidHide removal failed: $_"
    exit 1
}
