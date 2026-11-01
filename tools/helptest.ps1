param([string]$Exe, [string]$Out)

$ErrorActionPreference = 'Stop'

# End-to-end check that F1 in a running app opens the shared help file.
# Posts WM_KEYDOWN VK_F1 to the frame so the app's own accelerator table and
# command routing are exercised, then looks for an "HH Parent" window and
# captures it. Nothing else in the repo covers the app -> HtmlHelp path.

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class F1 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int m);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int m);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint command);
    public static bool IsOwnedBy(IntPtr h, uint processId) {
        uint owner;
        return GetWindowThreadProcessId(h, out owner) != 0 && owner == processId;
    }
    public static IntPtr FindMain(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (!IsOwnedBy(h, processId) || !IsWindowVisible(h) || GetWindow(h, 4) != IntPtr.Zero) return true;
            var title = new StringBuilder(512);
            var c = new StringBuilder(256);
            GetWindowText(h, title, 512);
            GetClassName(h, c, 256);
            if (c.ToString() != "HH Parent" && title.ToString().IndexOf("ImageWalker", StringComparison.OrdinalIgnoreCase) >= 0) {
                found = h;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr FindHelp(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner != processId) return true;
            var c = new StringBuilder(256);
            GetClassName(h, c, 256);
            if (c.ToString() == "HH Parent") { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

$p = $null
$frame = [IntPtr]::Zero
$hwnd = [IntPtr]::Zero
try {
    $path = (Resolve-Path -LiteralPath $Exe).Path
    $folder = Join-Path (Split-Path $PSScriptRoot) 'Help\images'
    $p = Start-Process -FilePath $path -ArgumentList ('"{0}"' -f $folder) -PassThru -WorkingDirectory (Split-Path $path)
    if (-not $p.WaitForInputIdle(30000)) { throw 'Application did not become idle.' }
    $deadline = (Get-Date).AddSeconds(25)
    while ($frame -eq [IntPtr]::Zero -and -not $p.WaitForExit(0) -and (Get-Date) -lt $deadline) {
        $frame = [F1]::FindMain([uint32]$p.Id)
        if ($frame -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    }
    if ($p.WaitForExit(0) -or $frame -eq [IntPtr]::Zero) { throw 'Application did not open a main window.' }
    $title = New-Object Text.StringBuilder 512
    [F1]::GetWindowText($frame, $title, 512) | Out-Null
    Write-Output "app: $title (PID $($p.Id))"

    # WM_KEYDOWN / WM_KEYUP, VK_F1
    if (-not [F1]::PostMessage($frame, 0x0100, [IntPtr]0x70, [IntPtr]0x003B0001) -or
        -not [F1]::PostMessage($frame, 0x0101, [IntPtr]0x70, [IntPtr]0xC03B0001)) {
        throw 'F1 could not be posted to the application.'
    }
    $deadline = (Get-Date).AddSeconds(20)
    while ($hwnd -eq [IntPtr]::Zero -and -not $p.WaitForExit(0) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
        $hwnd = [F1]::FindHelp([uint32]$p.Id)
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw 'No help window appeared for the application.' }
    $t = New-Object Text.StringBuilder 256
    [F1]::GetWindowText($hwnd, $t, 256) | Out-Null
    Write-Output "help: $t"
    if ($Out) {
        $r = New-Object F1+RECT
        if (-not [F1]::GetWindowRect($hwnd, [ref]$r)) { throw 'Help window bounds could not be read.' }
        $w = $r.R - $r.L; $h = $r.B - $r.T
        if ($w -le 0 -or $h -le 0) { throw 'Help window has no drawable area.' }
        $bmp = New-Object Drawing.Bitmap @($w, $h)
        try {
            $g = [Drawing.Graphics]::FromImage($bmp)
            try {
                $hdc = $g.GetHdc()
                try {
                    if (-not [F1]::PrintWindow($hwnd, $hdc, 2)) { throw 'Help window capture failed.' }
                }
                finally { $g.ReleaseHdc($hdc) }
            }
            finally { $g.Dispose() }
            $bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
        }
        finally { $bmp.Dispose() }
        Write-Output "saved: $Out"
    }
}
finally {
    if ($null -ne $p -and -not $p.WaitForExit(0)) {
        foreach ($window in @($hwnd, $frame)) {
            if ($window -ne [IntPtr]::Zero -and [F1]::IsOwnedBy($window, [uint32]$p.Id)) {
                [F1]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            }
        }
        if (-not $p.WaitForExit(5000)) {
            try { Stop-Process -Id $p.Id -Force -ErrorAction Stop }
            catch [Microsoft.PowerShell.Commands.ProcessCommandException] {
                if (-not $p.WaitForExit(0)) {
                    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive `
                        -Command "Stop-Process -Id $($p.Id) -Force -ErrorAction Stop"
                    if ($LASTEXITCODE -ne 0) { throw 'The owned application could not be stopped.' }
                }
            }
            if (-not $p.WaitForExit(5000)) { throw 'The owned application did not finish exiting.' }
        }
    }
}
Write-Output 'RESULT: PASS'
