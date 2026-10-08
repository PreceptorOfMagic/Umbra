# Deployed in <install>\scripts. No process is selected for termination by name alone.
$ErrorActionPreference = 'Stop'
try {
    $root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
    if ($root -eq [IO.Path]::GetPathRoot($root).TrimEnd('\')) { throw 'Refusing a drive root.' }
    for ($item = Get-Item -LiteralPath $root; $null -ne $item; $item = $item.Parent) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Installation uses a reparse point: $($item.FullName)" }
    }
    $relativePaths = @('sunshine.exe', 'aurora-link.exe', 'tools\sunshinesvc.exe', 'tools\dxgi-info.exe', 'tools\audio-info.exe', 'dispwake.exe', 'tools\dispwake.exe')
    $paths = @($relativePaths | ForEach-Object {
        $path = Join-Path $root $_
        # Include missing paths too: a running image can outlive its directory entry.
        if (Test-Path -LiteralPath $path) {
            $entry = Get-Item -LiteralPath $path
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Executable is a reparse point: $path" }
        }
        $path
    })
    $toolsDirectory = Join-Path $root 'tools'
    if ((Test-Path -LiteralPath $toolsDirectory) -and ((Get-Item -LiteralPath $toolsDirectory).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Tools directory is a reparse point.'
    }
    if (-not ('UmbraOwnedProcesses' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
public static class UmbraOwnedProcesses {
    const uint Synchronize = 0x00100000, Query = 0x1000, Terminate = 1;
    const uint Signalled = 0, Timeout = 258;
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder path, ref uint size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr OpenEvent(uint access, bool inherit, string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetEvent(IntPtr handle);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    sealed class Owned : IDisposable {
        public IntPtr Handle;
        public int Pid;
        public string Path;
        public bool Waitable;
        public bool Helper { get { return String.Equals(System.IO.Path.GetFileName(Path), "aurora-link.exe", StringComparison.OrdinalIgnoreCase); } }
        public void Dispose() { if (Handle != IntPtr.Zero) { CloseHandle(Handle); Handle=IntPtr.Zero; } }
    }
    static bool Exited(Owned p, uint timeout) {
        if (!p.Waitable) {
            uint code;
            if (!GetExitCodeProcess(p.Handle, out code)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot query candidate exit status " + p.Pid);
            return code != 259; // STILL_ACTIVE; this initial handle is never waited on or signalled.
        }
        uint result = WaitForSingleObject(p.Handle, timeout);
        if (result == Signalled) return true;
        if (result == Timeout) return false;
        throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot wait for owned process " + p.Pid);
    }
    static List<Owned> Find(string[] paths) {
        var wanted = new HashSet<string>(paths, StringComparer.OrdinalIgnoreCase);
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var found = new List<Owned>();
        try {
            foreach (string path in paths) names.Add(Path.GetFileNameWithoutExtension(path));
            foreach (string name in names) {
                foreach (Process candidate in Process.GetProcessesByName(name)) {
                    using (candidate) {
                        int pid = candidate.Id;
                        IntPtr handle = OpenProcess(Query, false, pid);
                        if (handle == IntPtr.Zero) {
                            int error = Marshal.GetLastWin32Error();
                            if (error == 87) continue; // PID disappeared before open; no signal is sent.
                            throw new Win32Exception(error, "Cannot inspect candidate " + name + " PID " + pid + " (Windows error " + error + "); nothing else will be stopped.");
                        }
                        var owned = new Owned { Handle=handle, Pid=pid };
                        try {
                            if (Exited(owned, 0)) continue;
                            var image = new StringBuilder(32768); uint length=(uint)image.Capacity;
                            if (!QueryFullProcessImageName(handle, 0, image, ref length)) {
                                int error=Marshal.GetLastWin32Error();
                                if (Exited(owned, 0)) continue;
                                throw new Win32Exception(error, "Cannot establish candidate identity for PID " + pid);
                            }
                            owned.Path=Path.GetFullPath(image.ToString());
                            if (wanted.Contains(owned.Path)) {
                                // Do not request termination rights on a foreign installation.
                                IntPtr control=OpenProcess(Synchronize | Query | Terminate, false, pid);
                                if (control == IntPtr.Zero) {
                                    int error=Marshal.GetLastWin32Error();
                                    if (Exited(owned, 0)) continue;
                                    throw new Win32Exception(error, "Cannot control owned process " + pid);
                                }
                                var controlled=new Owned { Handle=control, Pid=pid, Waitable=true };
                                try {
                                    if (Exited(controlled, 0)) continue;
                                    image.Clear(); length=(uint)image.Capacity;
                                    if (!QueryFullProcessImageName(control, 0, image, ref length)) {
                                        int error=Marshal.GetLastWin32Error();
                                        if (Exited(controlled, 0)) continue;
                                        throw new Win32Exception(error, "Cannot revalidate owned process " + pid);
                                    }
                                    controlled.Path=Path.GetFullPath(image.ToString());
                                    if (!String.Equals(owned.Path, controlled.Path, StringComparison.OrdinalIgnoreCase))
                                        throw new Exception("Owned process identity changed: " + pid);
                                    found.Add(controlled); controlled=null;
                                } finally { if (controlled != null) controlled.Dispose(); }
                            }
                        } finally { if (owned != null) owned.Dispose(); }
                    }
                }
            }
            return found;
        } catch { foreach (Owned p in found) p.Dispose(); throw; }
    }
    static void RequestHelperStop(Owned p) {
        if (Exited(p, 0)) return;
        IntPtr stop=OpenEvent(2, false, "Local\\EclipseLinkStop-" + p.Pid);
        if (stop == IntPtr.Zero) {
            int error=Marshal.GetLastWin32Error();
            if (error == 2 || Exited(p, 0)) return; // Older helper or another Windows session: host-death watcher/fallback.
            throw new Win32Exception(error, "Cannot open helper stop event for PID " + p.Pid);
        }
        try { if (!SetEvent(stop)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot signal helper PID " + p.Pid); }
        finally { CloseHandle(stop); }
    }
    static void ForceAndWait(Owned p) {
        if (Exited(p, 0)) return;
        if (!TerminateProcess(p.Handle, 1)) {
            int error=Marshal.GetLastWin32Error();
            if (!Exited(p, 0)) throw new Win32Exception(error, "Cannot stop owned process " + p.Pid);
        }
        if (!Exited(p, 5000)) throw new Exception("Owned process did not exit: " + p.Path + " PID " + p.Pid);
    }
    public static void Stop(string[] paths) {
        List<Owned> owned=Find(paths); // Validate all candidate identities before any termination.
        try {
            foreach (Owned p in owned) if (p.Helper) RequestHelperStop(p);
            // The service controller has already stopped the service. Standalone hosts and
            // diagnostic tools expose no common shutdown IPC; terminate their held handles.
            foreach (Owned p in owned) if (!p.Helper) ForceAndWait(p);
            var grace=Stopwatch.StartNew();
            foreach (Owned p in owned) if (p.Helper) {
                uint remaining=(uint)Math.Max(0, 5000-grace.ElapsedMilliseconds);
                if (!Exited(p, remaining)) ForceAndWait(p);
            }
        } finally { foreach (Owned p in owned) p.Dispose(); }
        List<Owned> remainingProcesses=Find(paths);
        try { if (remainingProcesses.Count != 0) throw new Exception("A process from this installation restarted during cleanup; files retained. Close it and retry."); }
        finally { foreach (Owned p in remainingProcesses) p.Dispose(); }
    }
}
'@
    }
    [UmbraOwnedProcesses]::Stop([string[]]$paths)
    exit 0
} catch {
    Write-Host "Owned process cleanup failed: $_"
    exit 1
}
