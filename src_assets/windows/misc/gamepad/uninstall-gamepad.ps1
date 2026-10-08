# Preserve the upstream optional removal choice, but report its actual result.
$ErrorActionPreference = 'Stop'
try {
    $products = @(Get-CimInstance -ClassName Win32_Product -Filter "Name='ViGEm Bus Driver'")
    $result = 0
    foreach ($product in $products) {
        $uninstall = Invoke-CimMethod -InputObject $product -MethodName Uninstall
        if ($null -eq $uninstall.ReturnValue) { throw 'Driver removal returned no status' }
        $code = [int]$uninstall.ReturnValue
        if ($code -ne 0 -and $code -ne 3010) { exit $code }
        if ($code -eq 3010) { $result = 3010 }
    }
    if ($products.Count -eq 0) { Write-Information 'ViGEm Bus Driver not found' }
    exit $result
} catch {
    Write-Host "ViGEm Bus Driver removal failed: $_"
    exit 1
}
