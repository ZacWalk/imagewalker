param([string]$Exe, [string]$Out)

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
    public static IntPtr FindHelp() {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            var c = new StringBuilder(256);
            GetClassName(h, c, 256);
            if (c.ToString() == "HH Parent") { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

Get-Process hh -ErrorAction SilentlyContinue | Stop-Process -Force
if ([F1]::FindHelp() -ne [IntPtr]::Zero) { throw "a help window was already open" }

$p = Start-Process -FilePath $Exe -PassThru -WorkingDirectory (Split-Path $Exe)
$p.WaitForInputIdle(30000) | Out-Null
$deadline = (Get-Date).AddSeconds(25)
while (-not $p.MainWindowHandle -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200; $p.Refresh() }
Start-Sleep -Seconds 4
$p.Refresh()
Write-Output "app: $($p.MainWindowTitle)"

# WM_KEYDOWN / WM_KEYUP, VK_F1
[F1]::PostMessage($p.MainWindowHandle, 0x0100, [IntPtr]0x70, [IntPtr]0x003B0001) | Out-Null
[F1]::PostMessage($p.MainWindowHandle, 0x0101, [IntPtr]0x70, [IntPtr]0xC03B0001) | Out-Null

$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(20)
while ($hwnd -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 400
    $hwnd = [F1]::FindHelp()
}

if ($hwnd -eq [IntPtr]::Zero) {
    Write-Output "RESULT: FAIL - no help window appeared"
} else {
    $t = New-Object Text.StringBuilder 256
    [F1]::GetWindowText($hwnd, $t, 256) | Out-Null
    Write-Output "help: $t"
    Start-Sleep -Seconds 2
    $r = New-Object F1+RECT
    [F1]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    if ($Out -and $w -gt 0 -and $h -gt 0) {
        $bmp = New-Object Drawing.Bitmap @($w, $h)
        $g = [Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [F1]::PrintWindow($hwnd, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose()
        $bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Output "saved: $Out"
    }
    Write-Output "RESULT: PASS"
}

Get-Process hh -ErrorAction SilentlyContinue | Stop-Process -Force
if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 800 }
if (-not $p.HasExited) { $p.Kill() }
