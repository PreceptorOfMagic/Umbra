# Check if a compatible version of ViGEmBus is already installed (1.17 or later)
try {
    $vigemBusPath = "$env:SystemRoot\System32\drivers\ViGEmBus.sys"
    $fileVersion = [System.Version](Get-Item $vigemBusPath -ErrorAction Stop).VersionInfo.FileVersion

    if ($fileVersion -ge [System.Version]"1.17") {
        Write-Information "The installed version is 1.17 or later, no update needed. Exiting."
        exit 0
    }
}
catch {
    Write-Information "ViGEmBus driver not found or inaccessible, proceeding with installation."
}

# Install Virtual Gamepad
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$installerPath = Join-Path $scriptPath "vigembus_installer.exe"
$installer = Start-Process `
    -FilePath $installerPath `
    -ArgumentList "/quiet", "/norestart" `
    -Wait -PassThru -ErrorAction Stop

# The prerequisite must finish before Umbra starts. Let NSIS handle failure
# and report a reboot requirement without restarting the machine unexpectedly.
exit $installer.ExitCode
