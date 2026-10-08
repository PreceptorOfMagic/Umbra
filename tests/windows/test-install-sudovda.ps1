# Harmless fixture executables only: never calls real certutil or nefconc.
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage = Join-Path ([IO.Path]::GetTempPath()) ('umbra display test-' + [guid]::NewGuid())
$oldPath = $env:PATH
New-Item -ItemType Directory $stage | Out-Null
try {
    Copy-Item (Join-Path $repo 'src_assets\windows\drivers\sudovda\install.bat') $stage
    Add-Type -OutputAssembly (Join-Path $stage 'certutil.exe') -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Reflection;
public class DisplayFixture {
    public static int Main(string[] args) {
        string dir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        string command = String.Join(" ", args);
        File.AppendAllText(Path.Combine(dir,"calls.txt"),command+"\n");
        string[] fail = File.ReadAllText(Path.Combine(dir,"fail.txt")).Split('|');
        return command.Contains(fail[0]) ? Int32.Parse(fail[1]) : 0;
    }
}
'@
    Copy-Item (Join-Path $stage 'certutil.exe') (Join-Path $stage 'nefconc.exe')
    $env:PATH = $stage + ';' + $oldPath
    foreach ($case in @(@('no-failure',0),@('-addstore -f root',5),@('TrustedPublisher',6),@('--create-device-node',7),@('--install-driver',8),@('--install-driver',3010))) {
        [IO.File]::WriteAllText((Join-Path $stage 'fail.txt'),($case -join '|'))
        Remove-Item (Join-Path $stage 'calls.txt') -ErrorAction SilentlyContinue
        & $env:ComSpec /d /c (Join-Path $stage 'install.bat') | Out-Null
        if ($LASTEXITCODE -ne $case[1]) { throw "Expected $($case[1]), got $LASTEXITCODE" }
        $calls = [IO.File]::ReadAllLines((Join-Path $stage 'calls.txt'))
        if ($case[1] -ne 0 -and $calls[-1] -notlike ('*' + $case[0] + '*')) { throw 'Continued after prerequisite failed' }
        Write-Host "PASS display prerequisite $($case[0]) -> $($case[1])"
    }
    Copy-Item (Join-Path $repo 'src_assets\windows\drivers\sudovda\uninstall.bat') $stage
    foreach ($code in @(0,5,3010)) {
        [IO.File]::WriteAllText((Join-Path $stage 'fail.txt'),("--remove-device-node|" + $code))
        & $env:ComSpec /d /c (Join-Path $stage 'uninstall.bat') | Out-Null
        if ($LASTEXITCODE -ne $code) { throw "Removal expected $code, got $LASTEXITCODE" }
        Write-Host "PASS display removal forwards $code without pausing"
    }
} finally {
    $env:PATH = $oldPath
    Remove-Item $stage -Recurse -Force
}
