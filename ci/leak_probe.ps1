<#
T78 内存泄漏排查基线脚本（04-delivery-plan.md 明文要求："反复打开关闭100个文档，看内存是否单调上升"）。

跑两轮 + 三条 M2 回归，共 5 组序列，每组 100 次采样：
  1. round1_fresh_process : 100 次全新进程，每次 --bench 打开 corpus 里的一份文档，
     读一次 private_bytes 后杀掉进程——验证"进程级无残留"。
  2. round2_inplace_replace: 同一进程内连续 100 次 openDocumentInPlace 文档替换
     （T36 路径，--bench-loop=replace:100），真正会暴露"进程内堆积"式泄漏的场景。
  3. m2_theme_loop         : 同一进程内连续 100 次主题切换（Ctrl+Shift+T 同路径）。
  4. m2_outline_loop       : 同一进程内连续 100 次大纲侧栏开关（Ctrl+\ 同路径）。
  5. m2_reload_loop        : 同一进程内连续 100 次 F5 重载（同路径）。

判据（比 M0/M1/M2 既有手法更严，见 08-m3-tasks.md T78）：
  - 前 10 次均值 vs 后 10 次均值，增幅 > 2% 判失败；
  - 且对整条序列做一元线性回归（x=迭代序号，y=private_bytes/MB），
    斜率 >= 0.05 MB/次 判失败——单纯比首尾均值会漏掉缓慢爬升。
  两者任一失败，该序列判定为"疑似泄漏"，脚本以 exit code 1 结束。

用法：
  powershell -File ci/leak_probe.ps1 [-N 100] [-ExePath <markair.exe>] [-CorpusDir <dir>]
              [-OutMarkdown <path>]
#>

param(
    [int]$N = 100,
    [string]$ExePath = "$PSScriptRoot\..\build\src\Release\markair.exe",
    [string]$CorpusDir = "$PSScriptRoot\..\bench\corpus",
    [string]$OutMarkdown = "$PSScriptRoot\..\bench\M3-LEAK.md"
)

$ErrorActionPreference = "Stop"

$ExePath = (Resolve-Path $ExePath).Path
$CorpusDir = (Resolve-Path $CorpusDir).Path
$corpusFiles = Get-ChildItem -Path $CorpusDir -Filter "*.md" | Sort-Object Name
if ($corpusFiles.Count -eq 0) { throw "corpus 目录下没有找到任何 .md 文件：$CorpusDir" }

# ---------------------------------------------------------------------------
# 统计工具：均值、增幅、一元线性回归斜率（单位 MB/次）。
# ---------------------------------------------------------------------------
function Get-Mean([double[]]$values) {
    if ($values.Count -eq 0) { return 0.0 }
    $sum = 0.0
    foreach ($v in $values) { $sum += $v }
    return $sum / $values.Count
}

function Get-LinearRegressionSlopeMBPerIter([double[]]$bytesSeries) {
    # x = 0..n-1（迭代序号），y = bytes/1MB。最小二乘法闭式解。
    $n = $bytesSeries.Count
    if ($n -lt 2) { return 0.0 }
    $xs = 0..($n - 1)
    $ys = $bytesSeries | ForEach-Object { $_ / 1MB }
    $meanX = Get-Mean ($xs | ForEach-Object { [double]$_ })
    $meanY = Get-Mean $ys
    $num = 0.0
    $den = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $dx = $xs[$i] - $meanX
        $num += $dx * ($ys[$i] - $meanY)
        $den += $dx * $dx
    }
    if ($den -eq 0.0) { return 0.0 }
    return $num / $den
}

# 判据：返回 @{ pass; firstTenMean; lastTenMean; growthPct; slopeMBPerIter }
function Test-LeakSeries([double[]]$bytesSeries) {
    $n = $bytesSeries.Count
    $firstTen = $bytesSeries[0..([Math]::Min(9, $n - 1))]
    $lastTen = $bytesSeries[([Math]::Max(0, $n - 10))..($n - 1)]
    $firstMean = Get-Mean $firstTen
    $lastMean = Get-Mean $lastTen
    $growthPct = if ($firstMean -eq 0) { 0.0 } else { (($lastMean - $firstMean) / $firstMean) * 100.0 }
    $slope = Get-LinearRegressionSlopeMBPerIter $bytesSeries
    $pass = ($growthPct -le 2.0) -and ($slope -lt 0.05)
    return [pscustomobject]@{
        pass           = $pass
        firstTenMeanMB = $firstMean / 1MB
        lastTenMeanMB  = $lastMean / 1MB
        growthPct      = $growthPct
        slopeMBPerIter = $slope
        n              = $n
    }
}

# ---------------------------------------------------------------------------
# 第一轮：100 次全新进程，每次打开 corpus 里循环取的一份文档，读一次
# private_bytes（EmitReport 的输出）后杀掉进程。
# ---------------------------------------------------------------------------
function Invoke-FreshProcessRound([int]$count) {
    $samples = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -lt $count; $i++) {
        $file = $corpusFiles[$i % $corpusFiles.Count].FullName
        $stderrFile = [System.IO.Path]::Combine($env:TEMP, "markair_leak_err_$([guid]::NewGuid().ToString('N')).txt")
        $proc = Start-Process -FilePath $ExePath `
            -ArgumentList @("--bench", "`"$file`"") `
            -RedirectStandardError $stderrFile -PassThru
        try {
            $deadline = (Get-Date).AddSeconds(15)
            $line = $null
            while ((Get-Date) -lt $deadline) {
                Start-Sleep -Milliseconds 80
                if (Test-Path $stderrFile) {
                    $content = Get-Content -Raw -Path $stderrFile -ErrorAction SilentlyContinue
                    if ($content -and $content -match "private_bytes=(\d+)") { $line = $Matches[1]; break }
                }
                if ($proc.HasExited) { break }
            }
            if (-not $line) { throw "第 $i 轮未能从 stderr 捕获到 private_bytes（超时或进程异常退出）。" }
            $samples.Add([double]$line)
        } finally {
            if (-not $proc.HasExited) { try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {} }
            Remove-Item $stderrFile -ErrorAction SilentlyContinue
        }
    }
    return $samples.ToArray()
}

# ---------------------------------------------------------------------------
# 第二轮/M2 三条回归：同一进程内连续 N 次动作，用 --bench-loop=<kind>:<count>
# 驱动（T78 新增，见 src/shell/window.cpp WM_TIMER 分支），从 stderr 里逐行
# 解析 "bench_loop_iter=<i> private_bytes=<n>"。
# ---------------------------------------------------------------------------
function Invoke-InProcessLoop([string]$kind, [int]$count, [string]$seedFile, [string]$corpusDirForReplace) {
    $stderrFile = [System.IO.Path]::Combine($env:TEMP, "markair_leak_loop_err_$([guid]::NewGuid().ToString('N')).txt")
    $args = @("--bench", "--bench-loop=$kind`:$count")
    if ($kind -eq "replace") { $args += "--bench-loop-dir=`"$corpusDirForReplace`"" }
    $args += "`"$seedFile`""

    $proc = Start-Process -FilePath $ExePath -ArgumentList $args `
        -RedirectStandardError $stderrFile -PassThru
    try {
        # 循环结束后 window.cpp 会自己投递 WM_CLOSE，进程应自然退出；
        # 给足够宽的超时（每次 30ms 节拍 * count，外加重排/重绘开销余量）。
        $deadline = (Get-Date).AddSeconds([Math]::Max(20, $count * 0.3))
        while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
            Start-Sleep -Milliseconds 100
        }
        $content = Get-Content -Raw -Path $stderrFile -ErrorAction SilentlyContinue
        if (-not $content) { throw "[$kind] 循环未产生任何 stderr 输出。" }
        $samples = New-Object System.Collections.Generic.List[double]
        foreach ($m in [regex]::Matches($content, "bench_loop_iter=(\d+) private_bytes=(\d+)")) {
            $samples.Add([double]$m.Groups[2].Value)
        }
        if ($samples.Count -lt $count) {
            throw "[$kind] 只采集到 $($samples.Count)/$count 个样本（进程可能中途退出或未正常完成循环）。"
        }
        return $samples.ToArray()
    } finally {
        if (-not $proc.HasExited) { try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {} }
        Remove-Item $stderrFile -ErrorAction SilentlyContinue
    }
}

Write-Host "=== T78 内存泄漏排查基线：round1(100 次全新进程) ==="
$round1 = Invoke-FreshProcessRound -count $N
$round1Judge = Test-LeakSeries $round1

Write-Host "=== T78: round2(同进程内连续 $N 次 openDocumentInPlace 换文档) ==="
$seed = $corpusFiles[0].FullName
$round2 = Invoke-InProcessLoop -kind "replace" -count $N -seedFile $seed -corpusDirForReplace $CorpusDir
$round2Judge = Test-LeakSeries $round2

Write-Host "=== T78: M2 回归 —— 主题切换 x$N ==="
$themeSeries = Invoke-InProcessLoop -kind "theme" -count $N -seedFile $seed -corpusDirForReplace $CorpusDir
$themeJudge = Test-LeakSeries $themeSeries

Write-Host "=== T78: M2 回归 —— 大纲侧栏开关 x$N ==="
$outlineSeries = Invoke-InProcessLoop -kind "outline" -count $N -seedFile $seed -corpusDirForReplace $CorpusDir
$outlineJudge = Test-LeakSeries $outlineSeries

Write-Host "=== T78: M2 回归 —— F5 重载 x$N ==="
$reloadSeries = Invoke-InProcessLoop -kind "reload" -count $N -seedFile $seed -corpusDirForReplace $CorpusDir
$reloadJudge = Test-LeakSeries $reloadSeries

$allResults = [ordered]@{
    round1_fresh_process    = @{ series = $round1;        judge = $round1Judge }
    round2_inplace_replace  = @{ series = $round2;        judge = $round2Judge }
    m2_theme_loop           = @{ series = $themeSeries;   judge = $themeJudge }
    m2_outline_loop         = @{ series = $outlineSeries; judge = $outlineJudge }
    m2_reload_loop          = @{ series = $reloadSeries;  judge = $reloadJudge }
}

foreach ($key in $allResults.Keys) {
    $j = $allResults[$key].judge
    $status = if ($j.pass) { "PASS" } else { "FAIL(疑似泄漏)" }
    Write-Host ("[{0}] n={1} 前10均值={2:F3}MB 后10均值={3:F3}MB 增幅={4:F2}% 回归斜率={5:F4}MB/次 => {6}" -f `
        $key, $j.n, $j.firstTenMeanMB, $j.lastTenMeanMB, $j.growthPct, $j.slopeMBPerIter, $status)
}

# ---------------------------------------------------------------------------
# 写 bench/M3-LEAK.md：完整序列 + 判据 + （若外部通过 -InjectLeakNote 传入）
# 双向验证记录占位——双向验证由外层脚本/人工调用本脚本两次并手工汇总，
# 这里只负责把"当前这一次跑出来的真实数据"落盘，不编造。
# ---------------------------------------------------------------------------
function Format-SeriesMarkdown([string]$name, $entry) {
    $j = $entry.judge
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("### $name")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("- 样本数: $($j.n)")
    [void]$sb.AppendLine("- 前10次均值: $([Math]::Round($j.firstTenMeanMB, 3)) MB")
    [void]$sb.AppendLine("- 后10次均值: $([Math]::Round($j.lastTenMeanMB, 3)) MB")
    [void]$sb.AppendLine("- 增幅: $([Math]::Round($j.growthPct, 3)) % （判据 <= 2%）")
    [void]$sb.AppendLine("- 线性回归斜率: $([Math]::Round($j.slopeMBPerIter, 5)) MB/次 （判据 < 0.05 MB/次）")
    [void]$sb.AppendLine("- 判定: $(if ($j.pass) { '通过' } else { '疑似泄漏' })")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("完整序列（private_bytes，字节）：")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine('```')
    [void]$sb.AppendLine(($entry.series -join ", "))
    [void]$sb.AppendLine('```')
    [void]$sb.AppendLine("")
    return $sb.ToString()
}

$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine("# M3-LEAK：内存泄漏排查基线（T78）")
[void]$md.AppendLine("")
[void]$md.AppendLine("生成时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[void]$md.AppendLine("")
[void]$md.AppendLine("## 判据")
[void]$md.AppendLine("")
[void]$md.AppendLine("- 前10次均值 vs 后10次均值，增幅 > 2% 判失败。")
[void]$md.AppendLine("- 对整条序列做一元线性回归（x=迭代序号，y=private_bytes/MB），斜率 >= 0.05 MB/次判失败。")
[void]$md.AppendLine("- 两者任一失败即判定该序列疑似泄漏。")
[void]$md.AppendLine("")
[void]$md.AppendLine("## 五组序列结果")
[void]$md.AppendLine("")
[void]$md.AppendLine("| 序列 | 样本数 | 前10均值(MB) | 后10均值(MB) | 增幅(%) | 回归斜率(MB/次) | 判定 |")
[void]$md.AppendLine("|---|---|---|---|---|---|---|")
foreach ($key in $allResults.Keys) {
    $j = $allResults[$key].judge
    $status = if ($j.pass) { "通过" } else { "疑似泄漏" }
    [void]$md.AppendLine(("| {0} | {1} | {2:F3} | {3:F3} | {4:F2} | {5:F4} | {6} |" -f `
        $key, $j.n, $j.firstTenMeanMB, $j.lastTenMeanMB, $j.growthPct, $j.slopeMBPerIter, $status))
}
[void]$md.AppendLine("")
[void]$md.AppendLine("## 完整序列数据")
[void]$md.AppendLine("")
foreach ($key in $allResults.Keys) {
    [void]$md.Append((Format-SeriesMarkdown $key $allResults[$key]))
}

Set-Content -Path $OutMarkdown -Value $md.ToString() -Encoding utf8
Write-Host "已写入 $OutMarkdown"

$anyFail = $allResults.Values | Where-Object { -not $_.judge.pass }
if ($anyFail) {
    Write-Host "存在疑似泄漏的序列，exit code = 1" -ForegroundColor Red
    exit 1
}
Write-Host "全部序列判定通过，exit code = 0" -ForegroundColor Green
exit 0
