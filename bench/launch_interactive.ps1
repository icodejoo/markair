# Helper script to launch mdvn on user's interactive desktop (WinSta0\Default)
param(
    [string]$FilePath = "D:\workspaces\mdvn\bench\demo.md"
)

Add-Type @"
using System;
using System.Runtime.InteropServices;

public class DesktopLauncher {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO {
        public Int32 cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public Int32 dwX;
        public Int32 dwY;
        public Int32 dwXSize;
        public Int32 dwYSize;
        public Int32 dwXCountChars;
        public Int32 dwYCountChars;
        public Int32 dwFillAttribute;
        public Int32 dwFlags;
        public Int16 wShowWindow;
        public Int16 cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION {
        public IntPtr hProcess;
        public IntPtr hThread;
        public Int32 dwProcessId;
        public Int32 dwThreadId;
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool CreateProcess(
        string lpApplicationName,
        string lpCommandLine,
        IntPtr lpProcessAttributes,
        IntPtr lpThreadAttributes,
        bool bInheritHandles,
        uint dwCreationFlags,
        IntPtr lpEnvironment,
        string lpCurrentDirectory,
        ref STARTUPINFO lpStartupInfo,
        out PROCESS_INFORMATION lpProcessInformation);

    public static int Launch(string appPath, string docPath) {
        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(si);
        si.lpDesktop = @"WinSta0\Default";
        PROCESS_INFORMATION pi;

        string cmdLine = string.IsNullOrEmpty(docPath) ? $"\"{appPath}\"" : $"\"{appPath}\" \"{docPath}\"";
        bool success = CreateProcess(null, cmdLine, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, null, ref si, out pi);
        if (!success) {
            return -Marshal.GetLastWin32Error();
        }
        return pi.dwProcessId;
    }
}
"@

$exePath = "D:\workspaces\mdvn\build\src\Release\mdvn.exe"
$pid = [DesktopLauncher]::Launch($exePath, $FilePath)
if ($pid -gt 0) {
    Write-Host "mdvn 已在用户交互桌面 (WinSta0\Default) 成功启动！PID: $pid"
} else {
    Write-Host "启动失败，错误码: $pid"
}
