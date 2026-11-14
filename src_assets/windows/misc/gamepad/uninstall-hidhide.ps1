# Removes HidHide only when Umbra setup installed it (install-hidhide.ps1 left a marker); a HidHide the user
# installed before Umbra stays. Never restarts the PC: MEASURED 2026-11-15 on VEGA, removing HidHide through
# Win32_Product.Uninstall (as uninstall-gamepad.ps1 does for ViGEmBus) returned 1641 and restarted Windows at once.
# msiexec with REBOOT=ReallySuppress reports 3010 instead, and NSIS offers the restart at the end.
$ErrorActionPreference = 'Stop'
$marker = Join-Path (Join-Path $env:ProgramData 'Umbra') 'hidhide-installed-by-umbra'
if (-not (Test-Path $marker)) {
    Write-Information 'HidHide was not installed by Umbra; leaving it.'
    exit 0
}
try {
    $keys = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    $codes = @(Get-ItemProperty $keys -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq 'HidHide' -and $_.PSChildName -match '^\{[0-9A-Fa-f-]{36}\}$' } |
        ForEach-Object { $_.PSChildName } | Sort-Object -Unique)
    $result = 0
    foreach ($code in $codes) {
        $p = Start-Process -FilePath 'msiexec.exe' -ArgumentList '/x', $code, '/qn', '/norestart', 'REBOOT=ReallySuppress' -Wait -PassThru
        if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { exit $p.ExitCode }
        if ($p.ExitCode -eq 3010) { $result = 3010 }
    }
    if ($codes.Count -eq 0) { Write-Information 'HidHide not found' }
    Remove-Item -Force $marker
    exit $result
} catch {
    Write-Host "HidHide removal failed: $_"
    exit 1
}
