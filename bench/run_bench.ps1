<#
.SYNOPSIS
    markair 性能基准测量脚本（T15）。

.DESCRIPTION
    暖启动模式（默认）：连续运行 markair.exe --bench <BENCH-A.md> N 次，
    解析每次运行 stderr 输出的 KV 性能行，计算各指标的中位数与 P95，
    输出一份逐轮原始数据 CSV，并在控制台打印摘要。

    冷启动模式（-Cold）：每轮运行前尝试调用 RAMMap64.exe -Et 清空
    Empty Standby List，模拟冷启动条件。若本机未安装 RAMMap，
    会打印提示后跳过清理步骤继续测量（不会报错退出），但此时的
    结果不能算作真正意义上的冷启动数据。

    暖/冷口径裁决（T71，见 08-m3-tasks.md「M3 裁决记录」#4）：
      - 暖启动 = "同一进程已运行过一次、文件已在系统缓存"，对应
        01-requirements.md §4 的"第二次及以后打开"。本脚本默认模式
        （不加 -Cold）近似这条口径：同一批 N 轮里，第 1 轮把文件读入
        系统文件缓存，第 2 轮起操作系统页缓存已命中，测的就是"文件已
        缓存"状态下的首屏时间（每轮仍是全新进程，"进程"本身不复用，
        复用的是文件系统缓存，这与"同一进程已运行过一次"字面不完全
        一致，此处口径偏差已如实记录于 bench\M3-METHOD.md）。
      - 冷启动 = 本参数 -Cold，每轮运行前先用 RAMMap 清 Empty Standby
        List，模拟"文件从未被读过、系统缓存为空"的场景。

    本脚本只负责测量与产出可读报告，不做通过/失败的阈值判定
    （阈值门禁是 T16 的范围）。

.PARAMETER N
    运行轮数，默认 20。

.PARAMETER Cold
    是否为冷启动模式。开启后每轮运行前尝试清 standby list。

.PARAMETER MarkairExe
    markair.exe 的路径，默认 build\src\Release\markair.exe（相对脚本所在的仓库根目录）。

.PARAMETER BenchFile
    用于测量的基准语料文件，默认 bench\BENCH-A.md。直接指定路径时优先级
    高于 -Target（两者都给时以 -BenchFile 为准）。

.PARAMETER Target
    语料预设名（T42 新增，T69 补充 BENCH-C，T71 补充 EMPTY，T73 补充
    BENCH-D），是 -BenchFile 的语法糖："BENCH-A"（默认，等价于原有行为，
    语料文件 bench\BENCH-A.md）、"BENCH-B"（图片密集场景，语料文件
    bench\BENCH-B.md，用于验证图片密集文档的峰值内存门槛）、"BENCH-C"
    （T54 新增的高亮最坏情况语料，语料文件 bench\BENCH-C.md，只用于记录
    趋势，不设门禁）、"EMPTY"（T71 新增，语料文件 bench\EMPTY.md，单行
    标题的极简文档，用于测 01 §4"常驻内存（空文档/刚启动）≤ 8MB"这一条
    —— 这条指标从 M0 到 M2 一次都没单独测过）、"BENCH-D"（T73 新增，语料
    文件 bench\BENCH-D.md，由 bench\make_bench_d.ps1 确定性拼接 bench\corpus
    下 39 份真实 README 生成的约 10MB 结构真实文档，按 M3 裁决 #8② 不进
    git，复跑前需先手动执行一次生成脚本，用于测 01 §4"10 MB 超大文档打开
    时间 ≤ 1.5s"这一条）。只在调用方未显式传 -BenchFile 时生效。

.PARAMETER SkipEnvInfo
    是否跳过环境信息采集（默认不跳过）。采集内容见下方"环境信息采集"说明。
    仅用于本地反复调试时节省时间，正式测量不应加这个开关。

.EXAMPLE
    powershell -File bench\run_bench.ps1 -N 20
    暖启动模式，跑 20 轮，输出 CSV 与中位数/P95 摘要（默认 BENCH-A）。

.EXAMPLE
    powershell -File bench\run_bench.ps1 -Cold -N 10
    冷启动模式，跑 10 轮；未装 RAMMap 时会自动降级为“未清 standby list”的测量。

.EXAMPLE
    powershell -File bench\run_bench.ps1 -Target BENCH-B -N 5
    图片密集场景（T42），等价于 -BenchFile bench\BENCH-B.md -N 5。

.EXAMPLE
    powershell -File bench\run_bench.ps1 -Target EMPTY -N 20
    空文档/刚启动内存基线（T71），等价于 -BenchFile bench\EMPTY.md -N 20。

.EXAMPLE
    powershell -File bench\run_bench.ps1 -Target BENCH-D -N 20
    10MB 超大文档首屏时间（T73），等价于 -BenchFile bench\BENCH-D.md -N 20；
    运行前需先执行一次 bench\make_bench_d.ps1 生成语料。
#>

param(
    [int]$N = 20,
    [switch]$Cold,
    [string]$MarkairExe = "build\src\Release\markair.exe",
    [string]$BenchFile,
    [ValidateSet("BENCH-A", "BENCH-B", "BENCH-C", "EMPTY", "BENCH-D")]
    [string]$Target = "BENCH-A",
    [string]$RamMapPath = "RAMMap64.exe",
    [switch]$SkipEnvInfo
)

$ErrorActionPreference = "Stop"

# -Target 只是 -BenchFile 的语法糖:调用方显式传了 -BenchFile 就以它为准
# （PSBoundParameters 能区分"没传"和"传了默认值"，不破坏原有的直接传路径用法）。
if (-not $PSBoundParameters.ContainsKey('BenchFile')) {
    $BenchFile = switch ($Target) {
        "BENCH-B" { "bench\BENCH-B.md" }
        "BENCH-C" { "bench\BENCH-C.md" }
        "EMPTY"   { "bench\EMPTY.md" }
        "BENCH-D" { "bench\BENCH-D.md" }
        default   { "bench\BENCH-A.md" }
    }
}

# 脚本自身所在目录即 bench\，仓库根目录是其上一级。
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

# 把相对路径统一解析到仓库根目录下，避免调用者当前目录不同导致找不到文件。
function Resolve-RepoPath([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return Join-Path $repoRoot $p
}

$markairExePath = Resolve-RepoPath $MarkairExe
$benchFilePath = Resolve-RepoPath $BenchFile

if (-not (Test-Path $markairExePath)) {
    throw "找不到 markair.exe：$markairExePath ；请先构建或用 -MarkairExe 指定正确路径。"
}
if (-not (Test-Path $benchFilePath)) {
    throw "找不到基准语料文件：$benchFilePath"
}

# 性能埋点（T14, src/app/bench.cpp EmitReport）输出的字段名，
# 一行 KV 文本，用正则从 stderr 全文里逐个提取，不假设该行是唯一输出。
$fieldNames = @(
    "t_process_to_parse_ms",
    "t_parse_to_layout_ms",
    "t_layout_to_window_ms",
    "t_window_to_present_ms",
    "t_process_to_present_ms",
    "private_bytes"
)

# 尝试定位 RAMMap64.exe：先看调用者指定/PATH，再看常见安装目录。
function Find-RamMap {
    $cmd = Get-Command $RamMapPath -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        "$env:ProgramFiles\RAMMap\RAMMap64.exe",
        "${env:ProgramFiles(x86)}\RAMMap\RAMMap64.exe",
        "$env:ProgramFiles\Sysinternals\RAMMap64.exe",
        "${env:ProgramFiles(x86)}\Sysinternals\RAMMap64.exe"
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    return $null
}

# 清一次 Empty Standby List；找不到 RAMMap 时打印提示但不中断脚本。
function Clear-StandbyList {
    $ramMap = Find-RamMap
    if (-not $ramMap) {
        Write-Warning "未找到 RAMMap64.exe，跳过清 standby list 这一步，本次测量不是真正冷启动。"
        return
    }
    Write-Host "调用 RAMMap 清 Empty Standby List：$ramMap -Et"
    try {
        & $ramMap -Et | Out-Null
    } catch {
        Write-Warning "调用 RAMMap 清 standby list 失败（$($_.Exception.Message)），继续测量但结果不是真正冷启动。"
    }
}

# 运行一次 markair.exe --bench <file>，捕获 stderr，解析出各字段值，
# 并主动杀掉进程（--bench 模式下窗口不会自动退出）。
function Invoke-OneRun([int]$index) {
    $stderrFile = [System.IO.Path]::Combine($env:TEMP, "markair_bench_err_$([guid]::NewGuid().ToString('N')).txt")
    $stdoutFile = [System.IO.Path]::Combine($env:TEMP, "markair_bench_out_$([guid]::NewGuid().ToString('N')).txt")

    $proc = Start-Process -FilePath $markairExePath `
        -ArgumentList @("--bench", "`"$benchFilePath`"") `
        -RedirectStandardError $stderrFile `
        -RedirectStandardOutput $stdoutFile `
        -PassThru

    try {
        # 首帧 Present 后进程会一直停在消息循环里等待用户操作，
        # 这里只等它把 bench 行写出来即可，不等待进程自然退出。
        $deadline = (Get-Date).AddSeconds(15)
        $line = $null
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 100
            if (Test-Path $stderrFile) {
                $content = Get-Content -Raw -Path $stderrFile -ErrorAction SilentlyContinue
                if ($content -and $content -match "t_process_to_present_ms=") {
                    $line = $content
                    break
                }
            }
            if ($proc.HasExited) { break }
        }

        if (-not $line) {
            if (Test-Path $stderrFile) { $line = Get-Content -Raw -Path $stderrFile -ErrorAction SilentlyContinue }
        }

        if (-not $line) {
            throw "第 $index 轮未能从 stderr 捕获到 bench 输出（超时或进程异常退出）。"
        }

        $result = [ordered]@{ run = $index }
        foreach ($f in $fieldNames) {
            if ($line -match "$f=([0-9.]+)") {
                $result[$f] = [double]$Matches[1]
            } else {
                throw "第 $index 轮 stderr 输出里找不到字段 '$f'，实际输出：`n$line"
            }
        }
        return [pscustomobject]$result
    } finally {
        # 无论解析是否成功，都要保证不留残留进程/窗口。
        if (-not $proc.HasExited) {
            try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
        }
        Remove-Item $stderrFile, $stdoutFile -ErrorAction SilentlyContinue
    }
}

# 中位数。
function Get-Median([double[]]$values) {
    $sorted = $values | Sort-Object
    $n = $sorted.Count
    if ($n -eq 0) { return $null }
    if ($n % 2 -eq 1) { return $sorted[[int](($n - 1) / 2)] }
    $a = $sorted[[int]($n / 2) - 1]
    $b = $sorted[[int]($n / 2)]
    return ($a + $b) / 2.0
}

# P95（最近秩插值法，对基准测量场景足够）。
function Get-P95([double[]]$values) {
    $sorted = $values | Sort-Object
    $n = $sorted.Count
    if ($n -eq 0) { return $null }
    $idx = [Math]::Ceiling(0.95 * $n) - 1
    if ($idx -lt 0) { $idx = 0 }
    if ($idx -gt $n - 1) { $idx = $n - 1 }
    return $sorted[$idx]
}

# 采集一份环境信息快照（T71 新增），写入与 CSV 同名前缀的 env_*.txt。
# 目的：数字如果脱离环境信息就没有可比性 —— memory.md 记录过搜狗输入法
# 全局注入导致私有内存 +30MB 的先例，任何全局注入模块都会让两次测量的
# private_bytes 无法直接比较，必须如实记录，而不是假设"环境是干净的"。
function Get-EnvInfoText {
    $lines = @()
    $lines += "==== markair 测量环境信息（T71）===="
    $lines += "采集时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

    try {
        $os = Get-CimInstance Win32_OperatingSystem
        $lines += "操作系统：$($os.Caption) $($os.Version) ($($os.OSArchitecture))"
    } catch { $lines += "操作系统：采集失败（$($_.Exception.Message)）" }

    try {
        $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
        $lines += "CPU：$($cpu.Name)，核心数 $($cpu.NumberOfCores)，逻辑处理器 $($cpu.NumberOfLogicalProcessors)"
    } catch { $lines += "CPU：采集失败（$($_.Exception.Message)）" }

    try {
        $mem = Get-CimInstance Win32_ComputerSystem
        $memGB = [Math]::Round($mem.TotalPhysicalMemory / 1GB, 2)
        $lines += "物理内存：$memGB GB"
    } catch { $lines += "物理内存：采集失败（$($_.Exception.Message)）" }

    try {
        $gpus = Get-CimInstance Win32_VideoController
        foreach ($g in $gpus) {
            $vramMB = if ($g.AdapterRAM) { [Math]::Round($g.AdapterRAM / 1MB, 0) } else { "未知" }
            $lines += "显卡：$($g.Name)，驱动版本 $($g.DriverVersion)，显存 $vramMB MB"
        }
    } catch { $lines += "显卡：采集失败（$($_.Exception.Message)）" }

    # SM_REMOTESESSION = 0x1000，用于判断是否在 RDP 会话中，
    # RDP 会话通常走软件渲染 / 网络显示驱动，会显著影响帧率与内存读数。
    try {
        Add-Type -Namespace Win32Native -Name NativeMethods -MemberDefinition @"
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int nIndex);
"@ -ErrorAction SilentlyContinue
        $isRdp = [Win32Native.NativeMethods]::GetSystemMetrics(0x1000)
        $lines += "是否 RDP 会话（SM_REMOTESESSION）：$(if ($isRdp -ne 0) { '是' } else { '否' })"
    } catch { $lines += "是否 RDP 会话：采集失败（$($_.Exception.Message)）" }

    # 全局注入模块排查：列出当前用户会话里常见的输入法/安全软件/美化工具
    # 相关进程，作为"是否存在全局注入模块"的间接证据（真正精确的办法是
    # 用 VMMap 对 markair.exe 本身做模块级快照，见 M3-METHOD.md 里对应条目）。
    try {
        $suspects = Get-Process -ErrorAction SilentlyContinue | Where-Object {
            $_.ProcessName -match "sogou|QQPCTray|360|baidu|wps|Tencent|ime|logi|razer"
        } | Select-Object -ExpandProperty ProcessName -Unique
        if ($suspects) {
            $lines += "疑似全局注入/常驻模块相关进程（间接证据，非精确列表）：$($suspects -join ', ')"
        } else {
            $lines += "疑似全局注入/常驻模块相关进程：未检测到（关键词匹配法，非精确）"
        }
    } catch { $lines += "全局注入模块排查：采集失败（$($_.Exception.Message)）" }

    return ($lines -join "`r`n")
}

# ------------------------- 主流程 -------------------------

$mode = if ($Cold) { "冷启动" } else { "暖启动" }
Write-Host "==== markair 性能基准测量：$mode 模式，共 $N 轮 ===="
Write-Host "markair.exe : $markairExePath"
Write-Host "语料文件 : $benchFilePath"

$results = @()
for ($i = 1; $i -le $N; $i++) {
    if ($Cold) {
        Clear-StandbyList
    }
    Write-Host "第 $i / $N 轮运行中..."
    $r = Invoke-OneRun -index $i
    $results += $r
}

# 输出逐轮原始数据 CSV。
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$csvPath = Join-Path $scriptDir "results_$timestamp.csv"
$results | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8
Write-Host ""
Write-Host "逐轮原始数据已写入：$csvPath"

# 环境信息快照（T71），与 CSV 同一时间戳，方便一一对应。
if (-not $SkipEnvInfo) {
    $envPath = Join-Path $scriptDir "env_$timestamp.txt"
    Get-EnvInfoText | Out-File -FilePath $envPath -Encoding UTF8
    Write-Host "环境信息快照已写入：$envPath"
} else {
    Write-Host "已跳过环境信息采集（-SkipEnvInfo）。"
}

# 计算并打印各字段的中位数 / P95 摘要。
Write-Host ""
Write-Host "==== 摘要（$mode，N=$N）===="
$summaryRows = @()
foreach ($f in $fieldNames) {
    $values = $results | ForEach-Object { $_.$f }
    $median = Get-Median $values
    $p95 = Get-P95 $values
    $summaryRows += [pscustomobject]@{
        field  = $f
        median = [Math]::Round($median, 3)
        p95    = [Math]::Round($p95, 3)
    }
}
$summaryRows | Format-Table -AutoSize

Write-Host "提示：本脚本仅产出可读测量报告，不做通过/失败阈值判定（阈值门禁见 T16）。"
