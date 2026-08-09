param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,
    [Parameter(Mandatory = $true)]
    [string]$RuntimeRoot,
    [switch]$SkipReopen
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class AocUiNative
{
    private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Point
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ScrollInfo
    {
        public uint CbSize;
        public uint FMask;
        public int NMin;
        public int NMax;
        public uint NPage;
        public int NPos;
        public int NTrackPos;
    }

    public sealed class WindowInfo
    {
        public IntPtr Handle { get; set; }
        public int Id { get; set; }
        public string ClassName { get; set; }
        public string Text { get; set; }
        public bool Visible { get; set; }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(IntPtr hwnd, StringBuilder className, int count);

    [DllImport("user32.dll")]
    private static extern int GetDlgCtrlID(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)]
    private static extern IntPtr SendMessageText(IntPtr hwnd, uint message, IntPtr wParam, StringBuilder text);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);

    [DllImport("user32.dll")]
    private static extern bool ScreenToClient(IntPtr hwnd, ref Point point);

    [DllImport("user32.dll")]
    private static extern IntPtr ChildWindowFromPointEx(IntPtr parent, Point point, uint flags);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);

    [DllImport("user32.dll")]
    private static extern bool GetScrollInfo(IntPtr hwnd, int bar, ref ScrollInfo info);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hwnd, int command);

    private static string WindowText(IntPtr hwnd)
    {
        int length = (int)SendMessage(hwnd, 0x000E, IntPtr.Zero, IntPtr.Zero).ToInt64(); // WM_GETTEXTLENGTH
        var buffer = new StringBuilder(Math.Max(512, length + 1));
        SendMessageText(hwnd, 0x000D, new IntPtr(buffer.Capacity), buffer); // WM_GETTEXT
        return buffer.ToString();
    }

    private static string ClassName(IntPtr hwnd)
    {
        var buffer = new StringBuilder(256);
        GetClassNameW(hwnd, buffer, buffer.Capacity);
        return buffer.ToString();
    }

    private static WindowInfo Describe(IntPtr hwnd)
    {
        return new WindowInfo {
            Handle = hwnd,
            Id = GetDlgCtrlID(hwnd),
            ClassName = ClassName(hwnd),
            Text = WindowText(hwnd),
            Visible = IsWindowVisible(hwnd),
        };
    }

    public static List<WindowInfo> WindowsForProcess(uint processId)
    {
        var result = new List<WindowInfo>();
        EnumWindows((hwnd, lParam) => {
            uint owner;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == processId) result.Add(Describe(hwnd));
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static List<WindowInfo> Children(IntPtr parent)
    {
        var result = new List<WindowInfo>();
        EnumChildWindows(parent, (hwnd, lParam) => {
            result.Add(Describe(hwnd));
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static string ReadText(IntPtr hwnd)
    {
        return WindowText(hwnd);
    }

    public static Rect ReadRect(IntPtr hwnd)
    {
        Rect rect;
        return GetWindowRect(hwnd, out rect) ? rect : new Rect();
    }

    public static IntPtr DeepestChildAtTargetCenter(IntPtr root, IntPtr target)
    {
        Rect rect;
        if (!GetWindowRect(target, out rect)) return IntPtr.Zero;
        var screenPoint = new Point {
            X = rect.Left + (rect.Right - rect.Left) / 2,
            Y = rect.Top + (rect.Bottom - rect.Top) / 2,
        };
        IntPtr current = root;
        for (int depth = 0; depth < 16; ++depth) {
            var localPoint = screenPoint;
            if (!ScreenToClient(current, ref localPoint)) break;
            IntPtr child = ChildWindowFromPointEx(current, localPoint, 0x0003); // skip invisible and disabled
            if (child == IntPtr.Zero || child == current) break;
            current = child;
        }
        return current;
    }

    public static int ReadScrollPosition(IntPtr hwnd)
    {
        var info = new ScrollInfo {
            CbSize = (uint)Marshal.SizeOf(typeof(ScrollInfo)),
            FMask = 0x17,
        };
        return GetScrollInfo(hwnd, 1, ref info) ? info.NPos : -1;
    }

    public static void SetText(IntPtr hwnd, string text)
    {
        IntPtr value = Marshal.StringToHGlobalUni(text);
        try { SendMessage(hwnd, 0x000C, IntPtr.Zero, value); }
        finally { Marshal.FreeHGlobal(value); }
    }

    public static void SendCommand(IntPtr parent, int id, int notification, IntPtr control)
    {
        long value = ((long)notification << 16) | (id & 0xffffL);
        SendMessage(parent, 0x0111, new IntPtr(value), control);
    }

    public static void SelectTabByKey(IntPtr tabs, int index)
    {
        SendMessage(tabs, 0x0100, new IntPtr(0x24), IntPtr.Zero); // WM_KEYDOWN/VK_HOME
        SendMessage(tabs, 0x0101, new IntPtr(0x24), IntPtr.Zero); // WM_KEYUP/VK_HOME
        for (int step = 0; step < index; ++step) {
            SendMessage(tabs, 0x0100, new IntPtr(0x27), IntPtr.Zero); // WM_KEYDOWN/VK_RIGHT
            SendMessage(tabs, 0x0101, new IntPtr(0x27), IntPtr.Zero); // WM_KEYUP/VK_RIGHT
        }
    }

}
'@

function Assert-TempTarget {
    param([string]$Path)
    $tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $full = [System.IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing runtime path outside TEMP: $full"
    }
    return $full
}

function Get-ChildWindows {
    param([IntPtr]$Parent)
    return @([AocUiNative]::Children($Parent))
}

function Get-Control {
    param(
        [IntPtr]$Parent,
        [int]$Id,
        [switch]$Visible
    )
    $matches = @(Get-ChildWindows $Parent | Where-Object { $_.Id -eq $Id })
    if ($matches.Count -eq 0) {
        $available = @(Get-ChildWindows $Parent | ForEach-Object { "$($_.Id):$($_.ClassName):$($_.Visible)" }) -join ', '
        $rect = [AocUiNative]::ReadRect($Parent)
        throw "Control ID $Id was not found. ParentText='$([AocUiNative]::ReadText($Parent))' ParentRect=$($rect.Left),$($rect.Top),$($rect.Right),$($rect.Bottom). Available controls: $available"
    }
    if ($Visible) {
        $matches = @($matches | Where-Object { $_.Visible })
        if ($matches.Count -eq 0) { throw "Control ID $Id is not visible on the active page" }
    }
    return $matches[0]
}

function Assert-SettingsPage {
    param(
        [IntPtr]$Window,
        [int]$Page
    )
    $signatures = @(
        @{ Expected = @(1000, 1002, 1004); Hidden = @(1011, 1030); Front = 1000; Text = 'Time format' },
        @{ Expected = @(1011, 1012, 1013); Hidden = @(1000, 1030); Front = 1011; Text = 'Movement mode' },
        @{ Expected = @(1030, 1031, 1032, 1033); Hidden = @(1000, 1011); Front = 1030; Text = 'Monitor' },
        @{ Expected = @(); Hidden = @(1000, 1011, 1030); Front = 0; Text = 'Exposure is charged only while' }
    )
    $signature = $signatures[$Page]
    $children = @(Get-ChildWindows $Window)
    foreach ($id in @(1034, 1035, 1036, 1037, 1038)) {
        if (@($children | Where-Object { $_.Id -eq $id -and $_.Visible }).Count -ne 1) {
            throw "Page $Page is missing visible footer control ID $id"
        }
    }
    $footerButton = Get-Control $Window 1034 -Visible
    $footerFront = [AocUiNative]::DeepestChildAtTargetCenter($Window, $footerButton.Handle)
    if ($footerFront -ne $footerButton.Handle) {
        throw "Page $Page footer is occluded in child Z-order (front=$footerFront expected=$($footerButton.Handle))"
    }
    if (@($children | Where-Object { $_.Visible -and $_.Text -eq 'Changes are saved automatically.' }).Count -ne 1) {
        throw "Page $Page is missing the automatic-save disclosure"
    }
    foreach ($id in $signature.Expected) {
        $matches = @($children | Where-Object { $_.Id -eq $id -and $_.Visible })
        if ($matches.Count -ne 1) {
            $visible = @($children | Where-Object { $_.Visible } | ForEach-Object { "$($_.Id):$($_.ClassName):$($_.Text)" }) -join ', '
            throw "Page $Page expected one visible control ID $id; found $($matches.Count). Visible: $visible"
        }
    }
    foreach ($id in $signature.Hidden) {
        if (@($children | Where-Object { $_.Id -eq $id -and $_.Visible }).Count -ne 0) {
            throw "Page $Page unexpectedly exposes control ID $id from another page"
        }
    }
    if (@($children | Where-Object { $_.Visible -and $_.Text.StartsWith($signature.Text) }).Count -eq 0) {
        throw "Page $Page is missing its visible '$($signature.Text)' signature"
    }
    $frontControl = $null
    if ($signature.Front -ne 0) {
        $control = Get-Control $Window $signature.Front -Visible
        $front = [AocUiNative]::DeepestChildAtTargetCenter($Window, $control.Handle)
        if ($front -ne $control.Handle) {
            throw "Page $Page control ID $($signature.Front) is occluded in child Z-order (front=$front expected=$($control.Handle))"
        }
        $frontControl = $signature.Front
    }
    return [ordered]@{
        Page = $Page
        ExpectedVisibleIds = $signature.Expected
        HiddenIds = $signature.Hidden
        FrontControlId = $frontControl
        SignatureText = $signature.Text
    }
}

function Save-WindowCapture {
    param(
        [IntPtr]$Window,
        [string]$Path
    )
    $rect = [AocUiNative]::ReadRect($Window)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { throw "Cannot capture invalid window bounds ${width}x${height}" }
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $dc = $graphics.GetHdc()
        try {
            if (-not [AocUiNative]::PrintWindow($Window, $dc, 2)) { throw 'PrintWindow failed' }
        }
        finally { $graphics.ReleaseHdc($dc) }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    $capture = Get-Item -LiteralPath $Path
    if ($capture.Length -lt 1000) { throw "Window capture is unexpectedly small: $($capture.Length) bytes" }
    return [ordered]@{ Path = $capture.FullName; Bytes = $capture.Length; Width = $width; Height = $height }
}

function Get-SettingsWindow {
    param([System.Diagnostics.Process]$Process)
    for ($attempt = 0; $attempt -lt 120; $attempt++) {
        $windows = @([AocUiNative]::WindowsForProcess([uint32]$Process.Id))
        $match = $windows | Where-Object {
            $_.Visible -and $_.ClassName -eq 'AdaptiveOledClockSettingsWindow' -and
            $_.Text -eq 'Adaptive OLED Clock Settings'
        } | Select-Object -First 1
        if ($null -ne $match) { return $match }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for the isolated Settings window"
}

function Test-ComboDropdown {
    param([object]$Combo)
    $before = [int64]([AocUiNative]::SendMessage($Combo.Handle, 0x0157, [IntPtr]::Zero, [IntPtr]::Zero))
    [AocUiNative]::SendMessage($Combo.Handle, 0x014F, [IntPtr]::new(1), [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 80
    $open = [int64]([AocUiNative]::SendMessage($Combo.Handle, 0x0157, [IntPtr]::Zero, [IntPtr]::Zero))
    [AocUiNative]::SendMessage($Combo.Handle, 0x014F, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 80
    $closed = [int64]([AocUiNative]::SendMessage($Combo.Handle, 0x0157, [IntPtr]::Zero, [IntPtr]::Zero))
    return [ordered]@{
        Id = $Combo.Id
        Text = $Combo.Text
        Before = ($before -ne 0)
        OpenAfterShow = ($open -ne 0)
        ClosedAfterHide = ($closed -eq 0)
    }
}

function Set-ComboSelection {
    param([IntPtr]$Window, [int]$Id, [int]$Index)
    $combo = Get-Control $Window $Id -Visible
    [AocUiNative]::SendMessage($combo.Handle, 0x014E, [IntPtr]::new($Index), [IntPtr]::Zero) | Out-Null
    [AocUiNative]::SendCommand($Window, $Id, 1, $combo.Handle) # CBN_SELCHANGE
    Start-Sleep -Milliseconds 120
}

function Read-SettingLine {
    param([string]$Path, [string]$Name)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $line = @(Get-Content -LiteralPath $Path | Where-Object { $_.StartsWith("$Name=") }) | Select-Object -First 1
    if ($null -eq $line) { return $null }
    return $line.Substring($Name.Length + 1)
}

function Read-OverlayRects {
    param([System.Diagnostics.Process]$Process)
    $windows = @([AocUiNative]::WindowsForProcess([uint32]$Process.Id))
    $rects = foreach ($window in ($windows | Where-Object { $_.ClassName -eq 'AdaptiveOledClockOverlayWindow' })) {
        $rect = [AocUiNative]::ReadRect($window.Handle)
        [ordered]@{
            Visible = $window.Visible
            Left = $rect.Left
            Top = $rect.Top
            Right = $rect.Right
            Bottom = $rect.Bottom
            Width = $rect.Right - $rect.Left
            Height = $rect.Bottom - $rect.Top
        }
    }
    return @($rects)
}

$exe = [System.IO.Path]::GetFullPath($ExePath)
$runtime = Assert-TempTarget $RuntimeRoot
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Executable not found: $exe" }
if (Test-Path -LiteralPath $runtime) {
    $item = Get-Item -LiteralPath $runtime -Force
    if (-not $item.PSIsContainer) { throw "Runtime target is not a directory: $runtime" }
    [System.IO.Directory]::Delete($runtime, $true)
}
New-Item -ItemType Directory -Path $runtime | Out-Null

$settingsPath = Join-Path $runtime 'AdaptiveOledClockCpp\settings.conf'
$process = $null
$evidence = [ordered]@{
    Executable = $exe
    RuntimeRoot = $runtime
    WindowFound = $false
    VisibleCombos = @()
    Dropdowns = @()
    PageChecks = @()
    VisualCaptures = @()
    Scroll = $null
    Movement = @{}
    CustomAllowedArea = @{}
    Reopened = @{}
    Overlay = @{}
}

function Start-IsolatedApp {
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    $info.Arguments = '--settings'
    $info.WorkingDirectory = Split-Path -Parent $exe
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.EnvironmentVariables['LOCALAPPDATA'] = $runtime
    $script:process = [System.Diagnostics.Process]::new()
    $script:process.StartInfo = $info
    if (-not $script:process.Start()) { throw "Failed to launch isolated Settings process" }
}

function Stop-IsolatedApp {
    if ($null -eq $script:process) { return }
    try {
        if (-not $script:process.HasExited) {
            $script:process.Kill()
            $script:process.WaitForExit(5000) | Out-Null
        }
    }
    finally {
        $script:process.Dispose()
        $script:process = $null
    }
}

try {
    Start-IsolatedApp
    $settings = Get-SettingsWindow $process
    $settingsHwnd = $settings.Handle
    $evidence.WindowFound = $true
    [AocUiNative]::ShowWindow($settingsHwnd, 5) | Out-Null
    [AocUiNative]::SetForegroundWindow($settingsHwnd) | Out-Null

    $tab = Get-Control $settingsHwnd 900
    for ($pageIndex = 0; $pageIndex -lt 4; $pageIndex++) {
        [AocUiNative]::SelectTabByKey($tab.Handle, $pageIndex)
        Start-Sleep -Milliseconds 120
        $evidence.PageChecks += Assert-SettingsPage $settingsHwnd $pageIndex
        $capturePath = Join-Path $runtime "settings-page-$pageIndex.png"
        $evidence.VisualCaptures += Save-WindowCapture $settingsHwnd $capturePath
    }
    [AocUiNative]::SelectTabByKey($tab.Handle, 0)
    Start-Sleep -Milliseconds 120
    $page0Combos = @(Get-ChildWindows $settingsHwnd | Where-Object { $_.Visible -and $_.ClassName -eq 'ComboBox' })
    $evidence.VisibleCombos += @($page0Combos | ForEach-Object { [ordered]@{ Page = 0; Id = $_.Id; Text = $_.Text } })
    $evidence.Dropdowns += @($page0Combos | ForEach-Object { Test-ComboDropdown $_ })

    $scrollTop = [AocUiNative]::ReadScrollPosition($settingsHwnd)
    [AocUiNative]::SendMessage($settingsHwnd, 0x0115, [IntPtr]::new(7), [IntPtr]::Zero) | Out-Null # WM_VSCROLL/SB_BOTTOM
    Start-Sleep -Milliseconds 100
    $scrollBottom = [AocUiNative]::ReadScrollPosition($settingsHwnd)
    [AocUiNative]::SendMessage($settingsHwnd, 0x0115, [IntPtr]::new(6), [IntPtr]::Zero) | Out-Null # WM_VSCROLL/SB_TOP
    Start-Sleep -Milliseconds 100
    $evidence.Scroll = [ordered]@{ Top = $scrollTop; Bottom = $scrollBottom; ReturnedTop = [AocUiNative]::ReadScrollPosition($settingsHwnd) }

    [AocUiNative]::SelectTabByKey($tab.Handle, 1)
    Start-Sleep -Milliseconds 120
    $page1Combos = @(Get-ChildWindows $settingsHwnd | Where-Object { $_.Visible -and $_.ClassName -eq 'ComboBox' })
    $evidence.VisibleCombos += @($page1Combos | ForEach-Object { [ordered]@{ Page = 1; Id = $_.Id; Text = $_.Text } })
    $evidence.Dropdowns += @($page1Combos | ForEach-Object { Test-ComboDropdown $_ })

    $page1ScrollTop = [AocUiNative]::ReadScrollPosition($settingsHwnd)
    [AocUiNative]::SendMessage($settingsHwnd, 0x0115, [IntPtr]::new(7), [IntPtr]::Zero) | Out-Null # WM_VSCROLL/SB_BOTTOM
    Start-Sleep -Milliseconds 100
    $page1ScrollBottom = [AocUiNative]::ReadScrollPosition($settingsHwnd)
    [AocUiNative]::SendMessage($settingsHwnd, 0x0115, [IntPtr]::new(6), [IntPtr]::Zero) | Out-Null # WM_VSCROLL/SB_TOP
    Start-Sleep -Milliseconds 100
    $evidence.Scroll = [ordered]@{
        Page0Top = $scrollTop
        Page0Bottom = $scrollBottom
        Page0ReturnedTop = [AocUiNative]::ReadScrollPosition($settingsHwnd)
        Page1Top = $page1ScrollTop
        Page1Bottom = $page1ScrollBottom
        Page1ReturnedTop = [AocUiNative]::ReadScrollPosition($settingsHwnd)
    }

    [AocUiNative]::SelectTabByKey($tab.Handle, 2)
    Start-Sleep -Milliseconds 120
    $page2Combos = @(Get-ChildWindows $settingsHwnd | Where-Object { $_.Visible -and $_.ClassName -eq 'ComboBox' })
    $evidence.VisibleCombos += @($page2Combos | ForEach-Object { [ordered]@{ Page = 2; Id = $_.Id; Text = $_.Text } })
    $evidence.Dropdowns += @($page2Combos | ForEach-Object { Test-ComboDropdown $_ })
    [AocUiNative]::SelectTabByKey($tab.Handle, 1)
    Start-Sleep -Milliseconds 120

    # Revision 198's contiguous enum assigns MovementMode=1011, AllowedPreset=1016,
    # and the four custom edit fields=1017..1020.
    $movementCombo = Get-Control $settingsHwnd 1011 -Visible
    Set-ComboSelection $settingsHwnd 1011 1 # Whole screen
    $wholeMode = Read-SettingLine $settingsPath 'movementMode'
    $wholeOverlay = Read-OverlayRects $process
    Set-ComboSelection $settingsHwnd 1011 0 # Edge-only
    $edgeMode = Read-SettingLine $settingsPath 'movementMode'
    $edgeOverlay = Read-OverlayRects $process
    $evidence.Movement = [ordered]@{
        MovementControlIndexAfterEdge = [int64]([AocUiNative]::SendMessage($movementCombo.Handle, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero))
        WholePersistedMovementMode = $wholeMode
        EdgePersistedMovementMode = $edgeMode
        WholeOverlayRects = $wholeOverlay
        EdgeOverlayRects = $edgeOverlay
    }

    Set-ComboSelection $settingsHwnd 1016 3 # Custom allowed area
    $customValues = [ordered]@{ 1017 = '10'; 1018 = '20'; 1019 = '80'; 1020 = '90' }
    $appliedCustomValues = [ordered]@{}
    foreach ($entry in $customValues.GetEnumerator()) {
        $edit = Get-Control $settingsHwnd ([int]$entry.Key) -Visible
        [AocUiNative]::SetText($edit.Handle, [string]$entry.Value)
        $writtenText = [AocUiNative]::ReadText($edit.Handle)
        if ($writtenText -ne [string]$entry.Value) {
            throw "Custom allowed-area control $($entry.Key) ($($edit.ClassName), handle=$($edit.Handle)) rejected '$($entry.Value)' and remains '$writtenText'"
        }
        [AocUiNative]::SendCommand($settingsHwnd, [int]$entry.Key, 0x0200, $edit.Handle) # EN_KILLFOCUS
        Start-Sleep -Milliseconds 120
        $appliedCustomValues.Add([string]$entry.Key, [string]$entry.Value)
        foreach ($applied in $appliedCustomValues.GetEnumerator()) {
            $actual = [AocUiNative]::ReadText((Get-Control $settingsHwnd ([int]$applied.Key) -Visible).Handle)
            if ([double]$actual -ne [double]$applied.Value) {
                throw "Custom allowed-area control $($applied.Key) changed unexpectedly: expected $($applied.Value), found $actual"
            }
        }
    }
    $evidence.CustomAllowedArea = [ordered]@{
        PersistedAllowedArea = Read-SettingLine $settingsPath 'allowedArea'
        PersistedMovementMode = Read-SettingLine $settingsPath 'movementMode'
        FieldValues = [ordered]@{
            Left = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1017 -Visible).Handle)
            Top = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1018 -Visible).Handle)
            Right = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1019 -Visible).Handle)
            Bottom = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1020 -Visible).Handle)
        }
        OverlayRects = Read-OverlayRects $process
    }

    [AocUiNative]::SendMessage($settingsHwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null # WM_CLOSE/hide
    Start-Sleep -Milliseconds 150
    Stop-IsolatedApp

    if ($SkipReopen) {
        $evidence.Reopened = [ordered]@{ Skipped = $true }
    }
    else {
        Start-IsolatedApp
        $settings = Get-SettingsWindow $process
        $settingsHwnd = $settings.Handle
        $tab = Get-Control $settingsHwnd 900
        [AocUiNative]::SelectTabByKey($tab.Handle, 1)
        Start-Sleep -Milliseconds 120
        $evidence.Reopened = [ordered]@{
            MovementIndex = [int64]([AocUiNative]::SendMessage((Get-Control $settingsHwnd 1011 -Visible).Handle, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero))
            AllowedPresetIndex = [int64]([AocUiNative]::SendMessage((Get-Control $settingsHwnd 1016 -Visible).Handle, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero))
            Left = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1017 -Visible).Handle)
            Top = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1018 -Visible).Handle)
            Right = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1019 -Visible).Handle)
            Bottom = [AocUiNative]::ReadText((Get-Control $settingsHwnd 1020 -Visible).Handle)
        }
    }
}
finally {
    Stop-IsolatedApp
}

$evidence.SettingsFile = $settingsPath
$evidence.SettingsFileExists = Test-Path -LiteralPath $settingsPath
$evidence.SettingsFileContent = if ($evidence.SettingsFileExists) { Get-Content -LiteralPath $settingsPath -Raw } else { $null }
$evidence.ProcessStopped = $true
$evidence | ConvertTo-Json -Depth 10
