<#
.SYNOPSIS
    mdvn 性能基准测量脚本（T15）。

.DESCRIPTION
    暖启动模式（默认）：连续运行 mdvn.exe --bench <BENCH-A.md> N 次，
    解析每次运行 stderr 输出的 KV 性能行，计算各指标的中位数与 P95，
    输出一份逐轮原始数据 CSV，并在控制台打印摘要。

    冷启动模式（-Cold）：每轮运行前尝试调用 RAMMap64.exe -Et 清空
    Empty Standby List，模拟冷启动条件。若本机未安装 RAMMap，
    会打印提示后跳过清理步骤继续测量（不会报错退出），但此时的
    结果不能算作真正意义上的冷启动数据。

    本脚本只负责测量与产出可读报告，不做通过/失败的阈值判定
    （阈值门禁是 T16 的范围）。

.PARAMETER N
    运行轮数，默认 20。

.PARAMETER Cold
    是否为冷启动模式。开启后每轮运行前尝试清 standby list。

.PARAMETER MdvnExe
    mdvn.exe 的路径，默认 build\src\Release\mdvn.exe（相对脚本所在的仓库根目录）。

.PARAMETER BenchFile
    用于测量的基准语料文件，默认 bench\BENCH-A.md。

.EXAMPLE
    powershell -File bench\run_bench.ps1 -N 20
    暖启动模式，跑 20 轮，输出 CSV 与中位数/P95 摘要。

.EXAMPLE
    powershell -File bench\run_bench.ps1 -Cold -N 10
    冷启动模式，跑 10 轮；未装 RAMMap 时会自动降级为“未清 standby list”的测量。
#>

param(
    [int]$N = 20,
    [switch]$Cold,
    [string]$MdvnExe = "build\src\Release\mdvn.exe",
    [string]$BenchFile = "bench\BENCH-A.md",
    [string]$RamMapPath = "RAMMap64.exe"
)

$ErrorActionPreference = "Stop"

# 脚本自身所在目录即 bench\，仓库根目录是其上一级。
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

# 把相对路径统一解析到仓库根目录下，避免调用者当前目录不同导致找不到文件。
function Resolve-RepoPath([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return Join-Path $repoRoot $p
}

$mdvnExePath = Resolve-RepoPath $MdvnExe
$benchFilePath = Resolve-RepoPath $BenchFile

if (-not (Test-Path $mdvnExePath)) {
    throw "找不到 mdvn.exe：$mdvnExePath ；请先构建或用 -MdvnExe 指定正确路径。"
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

# 运行一次 mdvn.exe --bench <file>，捕获 stderr，解析出各字段值，
# 并主动杀掉进程（--bench 模式下窗口不会自动退出）。
function Invoke-OneRun([int]$index) {
    $stderrFile = [System.IO.Path]::Combine($env:TEMP, "mdvn_bench_err_$([guid]::NewGuid().ToString('N')).txt")
    $stdoutFile = [System.IO.Path]::Combine($env:TEMP, "mdvn_bench_out_$([guid]::NewGuid().ToString('N')).txt")

    $proc = Start-Process -FilePath $mdvnExePath `
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

# ------------------------- 主流程 -------------------------

$mode = if ($Cold) { "冷启动" } else { "暖启动" }
Write-Host "==== mdvn 性能基准测量：$mode 模式，共 $N 轮 ===="
Write-Host "mdvn.exe : $mdvnExePath"
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
