# ui.ps1 - DeskTodo test helper (dot-source then use)
# usage: . F:\TodoList\tools\ui.ps1 ; Shot "F:\TodoList\out\x.png"
# NOTE: keep this file pure ASCII. Windows PowerShell 5.1 reads BOM-less scripts
# as ANSI(GBK), so UTF-8 Chinese comments corrupt the parse.

# Must run BEFORE Add-Type -AssemblyName System.Drawing: once GDI+ is initialised the
# process DPI awareness can no longer change. Without it this box (144dpi / 150%)
# returns 1/scale virtualised coords, so GetWindowRect / SetCursorPos / CopyFromScreen
# are all off and screenshots capture the wrong region.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DTDPI {
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
}
"@
[void][DTDPI]::SetProcessDpiAwarenessContext([IntPtr](-4))   # PER_MONITOR_AWARE_V2

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class DT {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, IntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

# find window by class name DeskTodoWnd (FindWindow is unreliable via P/Invoke here)
$script:DT_H = [IntPtr]::Zero
$script:DT_PID = 0
function Find-DeskTodo {
  $script:DT_H = [IntPtr]::Zero
  $cb = [DT+EnumProc]{
    param($h, $l)
    $sb = New-Object System.Text.StringBuilder 256
    [void][DT]::GetClassName($h, $sb, 256)
    if ($sb.ToString() -eq "DeskTodoWnd") {
      $pid2 = 0
      [void][DT]::GetWindowThreadProcessId($h, [ref]$pid2)
      $p = Get-Process -Id $pid2 -ErrorAction SilentlyContinue
      if ($p -and $p.ProcessName -eq "DeskTodo") { $script:DT_H = $h; $script:DT_PID = $pid2; return $false }
    }
    return $true
  }
  [void][DT]::EnumWindows($cb, [IntPtr]::Zero)
  return $script:DT_H
}

function DT-Rect {
  if ($script:DT_H -eq [IntPtr]::Zero) { [void](Find-DeskTodo) }
  $r = New-Object DT+RECT
  [void][DT]::GetWindowRect($script:DT_H, [ref]$r)
  return $r
}

function Shot($path) {
  $r = DT-Rect
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  return $path
}

# real cursor move is required: PostMessage(WM_MOUSEMOVE) does not produce hover
function Hover($x, $y) {
  $r = DT-Rect
  [void][DT]::SetCursorPos($r.L + $x, $r.T + $y)
  Start-Sleep -Milliseconds 220
}

function DTMouse($msg, $x, $y, $wp) {
  $lp = [IntPtr]((($y -band 0xFFFF) -shl 16) -bor ($x -band 0xFFFF))
  [void][DT]::PostMessage($script:DT_H, $msg, [IntPtr]$wp, $lp)
}

function Click($x, $y) {
  Hover $x $y
  DTMouse 0x0201 $x $y 1; Start-Sleep -Milliseconds 120
  DTMouse 0x0202 $x $y 0; Start-Sleep -Milliseconds 300
}

function DblClick($x, $y) {
  Hover $x $y
  DTMouse 0x0201 $x $y 1; Start-Sleep -Milliseconds 70
  DTMouse 0x0202 $x $y 0; Start-Sleep -Milliseconds 70
  DTMouse 0x0203 $x $y 1; Start-Sleep -Milliseconds 70
  DTMouse 0x0202 $x $y 0; Start-Sleep -Milliseconds 350
}

# real keyboard events: modal menu loops only accept real input
function RealClick($x, $y) {
  Hover $x $y
  [void][DT]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 90
  [void][DT]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 300
}

function RealKey($vk) {
  [DT]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80
  [DT]::keybd_event([byte]$vk, 0, 2, [IntPtr]::Zero); Start-Sleep -Milliseconds 250
}

function TypeText($s) {
  foreach ($c in $s.ToCharArray()) {
    [void][DT]::PostMessage($script:DT_H, 0x0102, [IntPtr][int]$c, [IntPtr]0)
  }
  Start-Sleep -Milliseconds 250
}

function PressKey($vk) {
  [void][DT]::PostMessage($script:DT_H, 0x0100, [IntPtr][int]$vk, [IntPtr]0)
  Start-Sleep -Milliseconds 250
}

function Dump-Data {
  (Get-Content "$env:APPDATA\DeskTodo\todos.json" -Raw -Encoding UTF8) -replace "\s+", " "
}
