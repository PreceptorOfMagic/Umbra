# HidHide lets Eclipse keep the controllers a player gives to the other co-op PC away from this PC's own games,
# when this PC is one of the two co-op PCs. It changes nothing on its own: nothing is hidden until Eclipse asks.
# Installed silently with Umbra, and removed with Umbra only if Umbra installed it (a marker under ProgramData).
$ErrorActionPreference = 'Stop'
$markerDir = Join-Path $env:ProgramData 'Umbra'
$marker = Join-Path $markerDir 'hidhide-installed-by-umbra'

if (Get-Service -Name 'HidHide' -ErrorAction SilentlyContinue) {
    Write-Information 'HidHide is already installed; leaving it as it is.'
    exit 0
}

$installerPath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'hidhide_installer.exe'
$installer = Start-Process -FilePath $installerPath -ArgumentList '/quiet', '/norestart' -Wait -PassThru
$code = $installer.ExitCode
if ($code -eq 0 -or $code -eq 3010) {
    New-Item -ItemType Directory -Force -Path $markerDir | Out-Null
    Set-Content -Path $marker -Value 'HidHide was installed by Umbra setup; Umbra removes it when Umbra is uninstalled.'
    # HidHide's installer puts its configuration tool on every user's desktop (MEASURED 2026-11-15). Umbra users
    # never need it -- Eclipse sets HidHide up itself -- so the shortcut goes; the tool stays in HidHide's folder.
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $env:PUBLIC 'Desktop\HidHide Configuration Client.lnk')
}
# 3010 = installed, a restart completes it; NSIS records it without restarting the PC.
exit $code
