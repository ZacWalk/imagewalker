param([string]$Chm, [string]$Out, [string]$MapId = "")

# Opens a .chm in the Windows help viewer and captures the window with
# PrintWindow, so a generated help file can be checked without a human looking
# at it. -MapId exercises the #IVB context map the same way the apps do.

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class HhShot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

$hhArgs = @()
if ($MapId) { $hhArgs += @('-mapid', $MapId) }
$hhArgs += $Chm

$p = Start-Process "$env:WINDIR\hh.exe" -ArgumentList $hhArgs -PassThru
$p.WaitForInputIdle(20000) | Out-Null
$deadline = (Get-Date).AddSeconds(15)
while (-not $p.MainWindowTitle -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200; $p.Refresh() }
Start-Sleep -Seconds 3
$p.Refresh()

Write-Output "title: $($p.MainWindowTitle)"

$r = New-Object HhShot+RECT
[HhShot]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L
$h = $r.B - $r.T
Write-Output "size: ${w}x${h}"

if ($w -gt 0 -and $h -gt 0) {
    $bmp = New-Object Drawing.Bitmap @($w, $h)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [HhShot]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "saved: $Out"
}

$p.CloseMainWindow() | Out-Null
Start-Sleep -Milliseconds 500
if (-not $p.HasExited) { $p.Kill() }
