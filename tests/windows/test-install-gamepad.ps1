# Runs the prerequisite script against a harmless compiled installer, never a driver.
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage = Join-Path ([IO.Path]::GetTempPath()) ('umbra-prerequisite-test-' + [guid]::NewGuid())
New-Item -ItemType Directory $stage | Out-Null
try {
    Copy-Item (Join-Path $repo 'src_assets\windows\misc\gamepad\install-gamepad.ps1') $stage
    $fixture = Join-Path $stage 'vigembus_installer.exe'
    Add-Type -OutputAssembly $fixture -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Threading;
public class InstallerFixture {
    public static int Main(string[] args) {
        File.WriteAllText(Environment.GetEnvironmentVariable("UMBRA_TEST_ARGS"), String.Join(" ", args));
        Thread.Sleep(600);
        return Int32.Parse(Environment.GetEnvironmentVariable("UMBRA_TEST_EXIT"));
    }
}
'@
    $env:UMBRA_TEST_ARGS = Join-Path $stage 'args.txt'
    # Scope the Get-Item override to this test; no system driver is read or changed.
    function Get-Item { throw 'Fixture: no installed driver' }
    foreach ($code in @(0, 3010, 1603)) {
        $env:UMBRA_TEST_EXIT = [string]$code
        $timer = [Diagnostics.Stopwatch]::StartNew()
        & (Join-Path $stage 'install-gamepad.ps1')
        $result = $LASTEXITCODE
        $timer.Stop()
        if ($result -ne $code) { throw "Exit code $code became $result" }
        if ($timer.ElapsedMilliseconds -lt 600) { throw 'Installer returned before prerequisite completed' }
        if ([IO.File]::ReadAllText($env:UMBRA_TEST_ARGS) -ne '/quiet /norestart') { throw 'Unexpected installer arguments' }
        Write-Host "PASS waited for prerequisite and propagated $code"
    }
    function Get-Item { return [pscustomobject]@{ VersionInfo = [pscustomobject]@{ FileVersion = '1.21.442.0' } } }
    Remove-Item $env:UMBRA_TEST_ARGS
    & (Join-Path $stage 'install-gamepad.ps1')
    if ($LASTEXITCODE -ne 0 -or (Test-Path $env:UMBRA_TEST_ARGS)) { throw 'Compatible shared driver was not left intact' }
    Write-Host 'PASS compatible shared driver is retained without starting installer'
} finally {
    Remove-Item Function:\Get-Item -ErrorAction SilentlyContinue
    Remove-Item Env:\UMBRA_TEST_ARGS, Env:\UMBRA_TEST_EXIT -ErrorAction SilentlyContinue
    Remove-Item $stage -Recurse -Force
}
