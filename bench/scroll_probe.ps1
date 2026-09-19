<#
.SYNOPSIS
    T72：向 markair 主窗口投递固定节奏的滚轮消息，模拟"持续滚动 N 秒"场景，
    供 PresentMon（方案A）或内置逐帧埋点（方案B）在同一段时间窗口内采集数据。

.DESCRIPTION
    按窗口类名 "markair_main_window" 定位目标窗口，然后用 PostMessage 向其投递
    WM_MOUSEWHEEL 消息，固定步长、固定间隔、固定总时长。

    ⚠️ 实现说明：本脚本用 EnumWindows + GetClassNameW 按类名匹配定位窗口，
    而不是直接调用 FindWindowW——本机实测 FindWindowW 对本项目窗口稳定返回
    NULL（GetLastError 也不是"未找到"该有的错误码），但同一进程里
    EnumWindows 能正常枚举到该窗口（用 GetClassNameW 核对类名一致），
    怀疑是本机某个 hook / 安全软件只拦截了 FindWindow 这一个高频被滥用的
    API。EnumWindows 定位到窗口后仍然用 PostMessage 投递，语义等价，
    没有引入 SendInput 或任何抢占输入焦点的手段。

    刻意不用 SendInput / mouse_event 这类"模拟真实输入设备"的手段——那类
    API 会抢占系统级的鼠标焦点，测试期间挪一下真实鼠标或切一下窗口都会
    污染这次测量；PostMessage 直接把消息塞进目标窗口的消息队列，不经过
    系统级输入焦点，不干扰操作者，且投递对象由窗口类名精确锁定，不会
    误伤其他窗口。

.PARAMETER ClassName
    目标窗口类名，默认 "markair_main_window"（与 src/shell/window.cpp 的
    kWindowClassName 保持一致）。

.PARAMETER DurationSeconds
    总投递时长（秒），默认 10（对应 01 §4 "滚动 60 FPS" 的测量窗口）。

.PARAMETER IntervalMs
    两次投递之间的间隔（毫秒），默认 16（约等于 60 FPS 的一帧间隔，
    让滚动事件的到达频率接近真实用户高频滚轮的场景）。

.PARAMETER SpinWait
    ⚠️ T76 修正，**测 60 FPS 时必须加这个开关**。默认的 `Start-Sleep
    -Milliseconds` 受 Windows 默认 15.625 ms 定时器粒度约束，请求 16 ms 实际
    会睡满两个 tick ≈ 31.25 ms —— T72 那轮"BENCH-A/BENCH-C 帧间隔都是
    30.8 ms、掉帧率 99%"就是这么来的：测到的根本不是 markair 的重绘能力，
    而是本脚本自己的投递节奏（10 秒 321 次 = 31.25 ms/次，与 P50 30.86 ms
    逐位吻合）。加上 `-SpinWait` 改用自旋等待，实测能稳定压到 16.0 ms，
    此时 markair 每条消息画一帧、一帧不掉（见 bench/M3-RENDER.md 第 3 节）。
    代价是本脚本自己占满一个核心，对被测进程的影响已在 M3-RENDER.md 里
    用"自旋 vs Start-Sleep 同语料对照"量化过。

.PARAMETER DeltaPerTick
    每次投递的滚轮增量，正数向上滚，负数向下滚。默认 -120（Windows 标准
    一个滚轮刻度 WHEEL_DELTA，方向向下，符合"从头往下滚"的典型场景）。

.EXAMPLE
    powershell -File bench\scroll_probe.ps1
    # 默认参数：对 markair_main_window 投递 10 秒、每 16ms 一次、向下滚动的滚轮消息。

.EXAMPLE
    powershell -File bench\scroll_probe.ps1 -DurationSeconds 10 -IntervalMs 16 -SpinWait
    # T76 之后测帧率的标准口径：真正按 16ms 投递，而不是被定时器粒度拖到 31ms。
#>

param(
    [string]$ClassName = "markair_main_window",
    [int]$DurationSeconds = 10,
    [int]$IntervalMs = 16,
    [int]$DeltaPerTick = -120,
    [switch]$SpinWait
)

# 内联 P/Invoke：EnumWindows + GetClassNameW 按类名定位窗口、PostMessageW 投递消息。
Add-Type -Namespace MarkairProbe -Name NativeMethods -MemberDefinition @'
public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

[DllImport("user32.dll")]
public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

[DllImport("user32.dll", CharSet = CharSet.Unicode)]
public static extern int GetClassNameW(IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);

[DllImport("user32.dll")]
public static extern bool PostMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

[DllImport("user32.dll")]
public static extern bool IsWindow(IntPtr hWnd);
'@

# WM_MOUSEWHEEL 消息号（Win32 常量，winuser.h）。
$WM_MOUSEWHEEL = 0x020A

# 按类名枚举定位窗口（见上方 DESCRIPTION 的说明：本机 FindWindowW 不可用，
# 改用 EnumWindows + GetClassNameW，语义等价）。
$found = [IntPtr]::Zero
$enumCb = {
    param($h, $l)
    $sb = New-Object System.Text.StringBuilder 256
    [void][MarkairProbe.NativeMethods]::GetClassNameW($h, $sb, 256)
    if ($sb.ToString() -eq $ClassName) {
        $script:found = $h
        return $false  # 找到即停止枚举
    }
    return $true
}
[void][MarkairProbe.NativeMethods]::EnumWindows($enumCb, [IntPtr]::Zero)
$hwnd = $found

if ($hwnd -eq [IntPtr]::Zero) {
    Write-Error "未找到窗口类名为 '$ClassName' 的窗口，请先启动 markair.exe（可加 --bench）。"
    exit 1
}
if (-not [MarkairProbe.NativeMethods]::IsWindow($hwnd)) {
    Write-Error "定位到的句柄无效。"
    exit 1
}

Write-Host "目标窗口句柄: 0x$($hwnd.ToString('X')), 类名: $ClassName"
Write-Host "开始投递 WM_MOUSEWHEEL：总时长 ${DurationSeconds}s，间隔 ${IntervalMs}ms，单次增量 $DeltaPerTick"

# wParam 高位是滚轮增量（有符号 16 位），低位是按键状态（此处为 0，不模拟修饰键）。
# PostMessage 的 wParam 类型是 UIntPtr/IntPtr，这里把 16 位有符号值放进高位，
# 全程用 uint32/int64 运算避免中间结果溢出 Int32。
$wheelParam = [int64]([uint32]($DeltaPerTick -band 0xFFFF)) * 65536
$wParam = [IntPtr][int64]$wheelParam
# lParam 是屏幕坐标 (x,y)，滚轮消息里坐标不影响 markair 的处理逻辑（整窗口滚动），
# 固定用 (0,0) 即可。
$lParam = [IntPtr]::Zero

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$totalMs = $DurationSeconds * 1000
$sentCount = 0

# 下一次投递的目标时刻（毫秒，相对 $sw 起点）；自旋模式按它对齐，避免误差累积。
$nextDueMs = 0.0

while ($sw.Elapsed.TotalMilliseconds -lt $totalMs) {
    if (-not [MarkairProbe.NativeMethods]::IsWindow($hwnd)) {
        Write-Error "目标窗口在投递过程中消失（可能已关闭），已投递 $sentCount 次后中止。"
        exit 1
    }
    [void][MarkairProbe.NativeMethods]::PostMessageW($hwnd, $WM_MOUSEWHEEL, $wParam, $lParam)
    $sentCount++
    $nextDueMs += $IntervalMs
    if ($SpinWait) {
        # 自旋到下一个目标时刻。见 .PARAMETER SpinWait：Start-Sleep 达不到 16ms。
        while ($sw.Elapsed.TotalMilliseconds -lt $nextDueMs) { }
    } else {
        Start-Sleep -Milliseconds $IntervalMs
    }
}

$sw.Stop()
$actualMs = $sw.Elapsed.TotalMilliseconds
$avgMs = if ($sentCount -gt 0) { [math]::Round($actualMs / $sentCount, 2) } else { 0 }
Write-Host "投递完成：共 $sentCount 次，实际耗时 $([math]::Round($actualMs)) ms，平均间隔 $avgMs ms。"
# 实际平均间隔明显大于请求值时直接告警——这正是 T72 那轮数据失真的成因，
# 不能再让它静悄悄地过去。
if ($sentCount -gt 0 -and $avgMs -gt ($IntervalMs * 1.25)) {
    Write-Warning ("实际平均投递间隔 $avgMs ms 远大于请求的 $IntervalMs ms（定时器粒度所致）。" +
                   "此时测出的帧间隔反映的是本脚本的投递节奏，不是 markair 的重绘能力；" +
                   "请加 -SpinWait 重测。")
}
