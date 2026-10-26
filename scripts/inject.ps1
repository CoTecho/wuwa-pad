
param(
    [string]$ProcessName = 'WuwaTappo',
    [string]$DllPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\wuwapad.dll'),
    [switch]$Elevated,
    [string]$ResultFile = (Join-Path $PSScriptRoot 'result.txt')
)

$src = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class WuwapadInjector {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct MODULEENTRY32 {
    public uint dwSize; public uint th32ModuleID; public uint th32ProcessID; public uint GlblcntUsage;
    public uint ProccntUsage; public IntPtr modBaseAddr; public uint modBaseSize; public IntPtr hModule;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string szModule;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string szExePath;
  }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool Module32First(IntPtr snap, ref MODULEENTRY32 me);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool Module32Next(IntPtr snap, ref MODULEENTRY32 me);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr addr, UIntPtr size, uint type, uint prot);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool VirtualFreeEx(IntPtr h, IntPtr addr, UIntPtr size, uint type);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteProcessMemory(IntPtr h, IntPtr addr, byte[] buf, UIntPtr size, out UIntPtr written);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr CreateRemoteThread(IntPtr h, IntPtr attr, UIntPtr stack, IntPtr start, IntPtr param, uint flags, out IntPtr tid);
  [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr h, uint ms);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetModuleHandleW(string n);
  [DllImport("kernel32.dll", CharSet=CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr m, string n);

  public static Dictionary<string, IntPtr> Modules(uint pid) {
    var d = new Dictionary<string, IntPtr>(StringComparer.OrdinalIgnoreCase);
    IntPtr snap = CreateToolhelp32Snapshot(0x8, pid);
    if (snap == (IntPtr)(-1)) return d;
    var me = new MODULEENTRY32();
    me.dwSize = (uint)Marshal.SizeOf(typeof(MODULEENTRY32));
    if (Module32First(snap, ref me)) { do { d[me.szModule] = me.modBaseAddr; } while (Module32Next(snap, ref me)); }
    CloseHandle(snap);
    return d;
  }

  public static string Inject(int pid, string dllPath) {
    var mods = Modules((uint)pid);
    string leaf = System.IO.Path.GetFileName(dllPath);
    if (mods.ContainsKey(leaf)) return "already-loaded:" + leaf;
    IntPtr kbRemote = IntPtr.Zero;
    foreach (var kv in mods) if (kv.Key.Equals("kernel32.dll", StringComparison.OrdinalIgnoreCase)) kbRemote = kv.Value;
    if (kbRemote == IntPtr.Zero) return "kernel32 not found (module list empty? count=" + mods.Count + ")";
    IntPtr kbLocal = GetModuleHandleW("kernel32.dll");
    IntPtr fnLocal = GetProcAddress(kbLocal, "LoadLibraryW");
    IntPtr fnRemote = new IntPtr(kbRemote.ToInt64() + (fnLocal.ToInt64() - kbLocal.ToInt64()));

    IntPtr h = OpenProcess(0x1F0FFF, false, pid);
    if (h == IntPtr.Zero) return "OpenProcess failed: " + Marshal.GetLastWin32Error() + " (need admin)";

    byte[] bytes = Encoding.Unicode.GetBytes(dllPath + "\0");
    IntPtr mem = VirtualAllocEx(h, IntPtr.Zero, (UIntPtr)bytes.Length, 0x3000, 0x04);
    if (mem == IntPtr.Zero) { CloseHandle(h); return "VirtualAllocEx failed: " + Marshal.GetLastWin32Error(); }
    UIntPtr written;
    if (!WriteProcessMemory(h, mem, bytes, (UIntPtr)bytes.Length, out written)) { CloseHandle(h); return "WriteProcessMemory failed: " + Marshal.GetLastWin32Error(); }

    IntPtr tid;
    IntPtr th = CreateRemoteThread(h, IntPtr.Zero, UIntPtr.Zero, fnRemote, mem, 0, out tid);
    if (th == IntPtr.Zero) { CloseHandle(h); return "CreateRemoteThread failed: " + Marshal.GetLastWin32Error(); }
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    VirtualFreeEx(h, mem, UIntPtr.Zero, 0x8000);
    CloseHandle(h);
    return "ok";
  }
}
'@
Add-Type -TypeDefinition $src -Language CSharp

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $Elevated -and -not $isAdmin) {
    $argsList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Elevated',
                  '-ProcessName',$ProcessName,'-DllPath',$DllPath,'-ResultFile',$ResultFile)
    try { Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argsList | Out-Null }
    catch { "elevation declined: $($_.Exception.Message)"; exit 2 }
    for ($i = 0; $i -lt 120; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-Path $ResultFile) { Start-Sleep -Milliseconds 200; Get-Content $ResultFile; exit 0 }
    }
    "timed out waiting for elevated injector"; exit 3
}

$lines = @()
$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { $lines += "target not running"; $lines | Set-Content $ResultFile; $lines; exit 1 }
$lines += "target pid: $($proc.Id)"
$lines += "modules before: " + ([WuwapadInjector]::Modules([uint32]$proc.Id)).Count
$lines += "inject: " + [WuwapadInjector]::Inject($proc.Id, $DllPath)
Start-Sleep -Milliseconds 700
$after = [WuwapadInjector]::Modules([uint32]$proc.Id)
$leaf = [System.IO.Path]::GetFileName($DllPath)
if ($after.ContainsKey($leaf)) { $lines += ("{0} at 0x{1:X}" -f $leaf, $after[$leaf].ToInt64()) } else { $lines += "$leaf NOT loaded" }
$lines | Set-Content $ResultFile
$lines
