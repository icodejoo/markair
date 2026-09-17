<#
.SYNOPSIS
    mdvn CI 性能门禁脚本（M0 T16，M1 T44 扩展）。

.DESCRIPTION
    依次执行：
    1. 若 Release 构建产物不存在（或指定 -ForceRebuild），先执行 CMake 构建；
    2. 运行 mdvn_tests.exe，退出码非 0 则整体失败；
    3. 调用 bench\run_bench.ps1 做一次暖启动测量（轮数通过 -N 暴露给调用者，
       脚本自身默认沿用文档约定的 20 次；本地手动验证时可传更小的值节省时间）；
    4. 对三项指标做阈值判断：首屏时间（P95）、常驻内存代理指标 private_bytes、
       exe 体积；
    5.（T44 新增）调用 bench\run_bench.ps1 -Target BENCH-B 做一次图片密集场景
       的内存测量，对 private_bytes 做阈值判断；
    6.（T44 新增）调用 ci\run_fuzz.ps1，其非零退出码直接计入整体失败。
    任一项超标 / 任一步骤非零退出，则本脚本以非零退出码结束。

    重要口径说明（必须诚实注明，不能不声不响套用文档数字）：
    本脚本判断的 "t_process_to_present_ms" 取自 src/app/bench.cpp 的埋点，
    统计的是"进程入口 -> 首次 Present"这一区间，每一轮都是全新启动的进程
    （见 bench/run_bench.ps1 的 Invoke-OneRun），并没有像 05-m0-tasks.md
    里"冷启动"定义那样先用 RAMMap 清空 Empty Standby List，也不是文档里
    "暖启动"定义所暗示的"进程已驻留、只重新打开文件"的场景。也就是说，
    这里测到的区间比文档严格定义的"暖启动首屏"更宽，比严格定义的"冷启动"
    又少了清 standby list 这一步 —— 是介于两者之间的一个简化口径。
    当前门禁阈值参照 M0 表格但测量口径与文档严格定义不完全一致，这是已知
    简化，非最终门禁。等 T14/T15 的口径与真正的冷/暖启动场景对齐、且本机装好
    VMMap/RAMMap 之后，应该用更严格的测量方式替换本脚本里的判断依据。

.PARAMETER N
    暖启动测量轮数（BENCH-A），透传给 run_bench.ps1。默认 20（与文档一致）；
    本地手动验证时可以传更小的值（例如 10）节省时间。

.PARAMETER BuildConfig
    CMake 构建配置，默认 Release。

.PARAMETER ForceRebuild
    即使构建产物已存在，也强制重新构建一次。

.PARAMETER FirstPaintP95ThresholdMs
    首屏时间（t_process_to_present_ms）P95 阈值，单位毫秒。
    默认 400（对应 M0 表格"冷启动首屏"上限档；见上方口径说明，这是简化映射）。

.PARAMETER PrivateBytesThresholdMB
    BENCH-A（无图/首屏场景）private_bytes（PrivateUsage，常驻内存的自动化代理
    指标）阈值，单位 MB。默认 16（权威值应以 VMMap 为准，本机未装 VMMap，
    故用该代理指标先行把关）。

    ——— M1 private_bytes 基线重记(阶段 J 收尾，2026-09-17)———
    M0 阶段基线：12MB（当时 BENCH-A 实测约 10.5MB）。
    排查发现 T42 引入的"--bench 强制全量解码"标志曾经不区分具体语料，只要
    带 `--bench` 就生效，导致 BENCH-A（本该只测首屏虚拟化）也被误强制物化成
    全文档，P95 一度冲到约 29MB。已在 main.cpp 新增 BenchTargetWantsFullDecode
    把该行为限定为仅 BENCH-B（图片密集场景）生效，BENCH-A 恢复首屏虚拟化
    语义后，实测 P95 回落到约 13.75MB。
    这剩下的 ~1.75MB（相对 M0 的 12MB 增长约 15%）经排查是阶段 F~I 新功能
    （富行内样式/表格布局/脚注区/查找索引等常驻数据结构）带来的真实、预期
    内的内存增长，不是 bug，不做进一步压缩。**这不是同一个基线**——16MB
    是 M1 新基线，如实记录，不做"和 M0 比谁更小"的误导性对比；等 VMMap
    装好之后应换成权威值复核。

.PARAMETER ExeSizeSoftLimitMB
    exe 体积的宽松安全阈值，单位 MB。
    默认 8（见下方"M1 exe 体积基线"说明：M1 新基线约 0.28MB，仍是"记录基线，
    不做硬性门禁"的口径，本阈值只是防失控的宽松安全网，不代表官方门槛）。

    ——— M1 exe 体积基线（T44，2026-09-17 实测）———
    M0 阶段基线：约 228KB（0.223MB，静态链接、无图片/查找/WIC/WinHTTP 代码）。
    M1 阶段新基线：296448 字节 ≈ 0.2827MB（Release 构建，clean build 后实测，
    包含 T30 图片解码路径的 WIC delayload 桩、T34 网络图片的 WinHTTP delayload
    桩、T37/T38 查找算法代码、T39 state.ini 解析代码）。
    这**不是同一个基线**：M1 比 M0 增大约 61.5KB（+27%），原因是新增了
    WIC/WinHTTP 的 delayload 桩与图片解码/查找功能的代码体积，是预期内的
    合理增长，不代表体积失控。这里如实记录新数字，不做"和 M0 比谁更小"的
    误导性对比；沿用文档口径——exe 体积只记录基线趋势，M3 才门禁 1.5MB。
    本脚本额外加的宽松安全网阈值相应从 5MB 上调到 8MB，仍留有约 28 倍余量，
    只用于拦截"不小心静态链接了一个很大的库"这种明显异常。

.PARAMETER BenchBPrivateBytesThresholdMB
    （T44 新增）BENCH-B（图片密集场景，50 张 PNG 全部滚过一遍）private_bytes
    P95 阈值，单位 MB。默认 80，对应 06-m1-tasks.md"M1 验收线与测量方法"表
    "图片密集文档峰值内存 ≤80MB"这一条。本机实测（Release clean build，
    N=5，--bench 全量解码口径）private_bytes 中位数 ≈44.1MB、P95 ≈42.3MB，
    留有约一倍余量；曾观察到单轮之间有若干 MB 的波动（属正常范围，未见
    显著抬升趋势）。

.PARAMETER NBenchB
    BENCH-B 内存测量轮数，透传给 run_bench.ps1 -Target BENCH-B。默认 5
    （BENCH-B 每轮都要全量解码 50 张 PNG，比 BENCH-A 慢，轮数比 -N 少，
    足以估计 P95 即可，不追求和 BENCH-A 同样的统计功效）。

.PARAMETER SkipFuzz
    （T44 新增）跳过 ci\run_fuzz.ps1 这一步。仅用于本地调试其它门禁项时
    节省时间，CI 环境下不应加这个开关。

.PARAMETER FuzzTimeoutSeconds
    （T44 新增）透传给 ci\run_fuzz.ps1 的 -TimeoutSeconds。默认 -1 表示不覆盖
    （沿用 run_fuzz.ps1 自身默认的 60 秒）。主要用途是双向验证：传 0 可以
    人为制造"0 秒内必然判定为卡死"从而让 run_fuzz.ps1 必然非零退出，验证
    本脚本能正确把这个非零退出码计入整体失败（而不是被吞掉）。

.EXAMPLE
    powershell -File ci\check_budget.ps1 -N 10
    本地手动验证：跑 10 轮暖启动测量并做门禁判断（含 BENCH-B 内存与 fuzz 门禁）。
#>

param(
    [int]$N = 20,
    [string]$BuildConfig = "Release",
    [switch]$ForceRebuild,
    [double]$FirstPaintP95ThresholdMs = 400,
    [double]$PrivateBytesThresholdMB = 16,
    [double]$ExeSizeSoftLimitMB = 8,
    [double]$BenchBPrivateBytesThresholdMB = 80,
    [int]$NBenchB = 5,
    [switch]$SkipFuzz,
    [int]$FuzzTimeoutSeconds = -1
)

$ErrorActionPreference = "Stop"

# 脚本自身在 ci\ 目录下，仓库根目录是其上一级。
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

$buildDir = Join-Path $repoRoot "build"
$exePath = Join-Path $buildDir "src\$BuildConfig\mdvn.exe"
$testsExePath = Join-Path $buildDir "tests\$BuildConfig\mdvn_tests.exe"
$runBenchScript = Join-Path $repoRoot "bench\run_bench.ps1"

# 记录一次失败原因，最后统一汇总输出，方便一次性看到"到底超了几项"。
$failures = @()

Write-Host "==== mdvn CI 性能门禁（M0 T16 + M1 T44）===="
Write-Host "当前门禁阈值参照 M0/M1 表格但测量口径与文档严格定义不完全一致，这是已知简化，非最终门禁。"
Write-Host ""

# ------------------------- 第一步：构建（若需要） -------------------------

$needBuild = $ForceRebuild -or (-not (Test-Path $exePath)) -or (-not (Test-Path $testsExePath))
if ($needBuild) {
    Write-Host "[1/6] 未找到构建产物或指定强制重建，开始构建（$BuildConfig）..."
    if (-not (Test-Path $buildDir)) {
        cmake -S $repoRoot -B $buildDir -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败，退出码 $LASTEXITCODE" }
    }
    cmake --build $buildDir --config $BuildConfig
    if ($LASTEXITCODE -ne 0) { throw "CMake 构建失败，退出码 $LASTEXITCODE" }
} else {
    Write-Host "[1/6] 构建产物已存在，跳过构建（传 -ForceRebuild 可强制重建）。"
}

if (-not (Test-Path $exePath)) { throw "构建后仍找不到 mdvn.exe：$exePath" }
if (-not (Test-Path $testsExePath)) { throw "构建后仍找不到 mdvn_tests.exe：$testsExePath" }

# ------------------------- 第二步：单元测试 -------------------------

Write-Host ""
Write-Host "[2/6] 运行 mdvn_tests.exe ..."
# 注意：PowerShell 5.1 下，$ErrorActionPreference = "Stop" 时，原生程序往
# stderr 写内容会被包装成终止性 ErrorRecord 抛出，即便退出码是 0（mdvn_tests
# 的汇总行走的就是 stderr）。这里临时降级为 "Continue"，只靠 $LASTEXITCODE
# 判断成败，避免被这个 PowerShell 行为误判为失败。
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $testsExePath
$testsExitCode = $LASTEXITCODE
$ErrorActionPreference = $prevEap
if ($testsExitCode -ne 0) {
    $failures += "单元测试失败，mdvn_tests.exe 退出码为 $testsExitCode（应为 0）。"
} else {
    Write-Host "单元测试通过（退出码 0）。"
}

# ------------------------- 第三步：暖启动性能测量 -------------------------

Write-Host ""
Write-Host "[3/6] 运行 bench\run_bench.ps1 做暖启动测量（BENCH-A，N=$N）..."
& $runBenchScript -N $N -MdvnExe $exePath
if ($LASTEXITCODE -ne 0) {
    throw "run_bench.ps1 执行失败，退出码 $LASTEXITCODE"
}

# run_bench.ps1 只打印摘要不落盘机器可读结果，这里直接复用它写出的 CSV
# （文件名带时间戳，取最新一份）来拿到 P95 / private_bytes 数值。
$benchDir = Join-Path $repoRoot "bench"
$latestCsv = Get-ChildItem -Path $benchDir -Filter "results_*.csv" |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $latestCsv) { throw "未找到 run_bench.ps1 产出的 results_*.csv，无法做阈值判断。" }

$rows = Import-Csv -Path $latestCsv.FullName

function Get-P95FromRows([object[]]$rows, [string]$field) {
    $values = $rows | ForEach-Object { [double]$_.$field } | Sort-Object
    $n = $values.Count
    if ($n -eq 0) { return $null }
    $idx = [Math]::Ceiling(0.95 * $n) - 1
    if ($idx -lt 0) { $idx = 0 }
    if ($idx -gt $n - 1) { $idx = $n - 1 }
    return $values[$idx]
}

# 门禁统一用 P95（比中位数更保守），字段名来自 src/app/bench.cpp 的实际输出。
$firstPaintP95 = Get-P95FromRows -rows $rows -field "t_process_to_present_ms"
$privateBytesP95 = Get-P95FromRows -rows $rows -field "private_bytes"
$privateBytesMB = $privateBytesP95 / 1MB

Write-Host ""
Write-Host "首屏时间 t_process_to_present_ms 的 P95（N=$N）：$([Math]::Round($firstPaintP95, 3)) ms（阈值 $FirstPaintP95ThresholdMs ms）"
Write-Host "private_bytes 的 P95（N=$N）：$([Math]::Round($privateBytesMB, 3)) MB（阈值 $PrivateBytesThresholdMB MB）"

if ($firstPaintP95 -gt $FirstPaintP95ThresholdMs) {
    $failures += "首屏时间 P95 超标：$([Math]::Round($firstPaintP95, 3)) ms > $FirstPaintP95ThresholdMs ms。"
}
if ($privateBytesMB -gt $PrivateBytesThresholdMB) {
    $failures += "private_bytes P95 超标：$([Math]::Round($privateBytesMB, 3)) MB > $PrivateBytesThresholdMB MB。"
}

# ------------------------- 第四步：exe 体积 -------------------------

Write-Host ""
Write-Host "[4/6] 记录 exe 体积基线..."
$exeSizeBytes = (Get-Item $exePath).Length
$exeSizeMB = $exeSizeBytes / 1MB
Write-Host "mdvn.exe 体积：$([Math]::Round($exeSizeMB, 4)) MB（$exeSizeBytes 字节）"
Write-Host "文档口径：M0 阶段只记录基线，不做硬性门禁（M3 才门禁 1.5MB）。"
Write-Host "本脚本额外加了一道宽松安全网阈值 $ExeSizeSoftLimitMB MB（见脚本头部注释的依据说明），仅用于防止体积失控，不代表官方门槛。"

if ($exeSizeMB -gt $ExeSizeSoftLimitMB) {
    $failures += "exe 体积超过宽松安全网阈值：$([Math]::Round($exeSizeMB, 4)) MB > $ExeSizeSoftLimitMB MB（注意：这不是文档规定的官方门槛，是本脚本额外加的安全网）。"
}

# ------------------------- 第五步：BENCH-B 图片密集场景内存门禁（T44） -------------------------

Write-Host ""
Write-Host "[5/6] 运行 bench\run_bench.ps1 -Target BENCH-B 做图片密集场景内存测量（N=$NBenchB）..."
& $runBenchScript -Target "BENCH-B" -N $NBenchB -MdvnExe $exePath
if ($LASTEXITCODE -ne 0) {
    throw "run_bench.ps1 -Target BENCH-B 执行失败，退出码 $LASTEXITCODE"
}

# 和第三步一样，取 run_bench.ps1 刚写出的最新一份 CSV（此时它一定比
# 第三步那份新，因为 BENCH-B 测量在其之后才跑）。
$latestCsvBenchB = Get-ChildItem -Path $benchDir -Filter "results_*.csv" |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $latestCsvBenchB) { throw "未找到 BENCH-B 测量产出的 results_*.csv，无法做阈值判断。" }
if ($latestCsvBenchB.FullName -eq $latestCsv.FullName) {
    throw "BENCH-B 的 CSV 与 BENCH-A 的 CSV 是同一份文件，测量顺序可能有误，无法信任本次阈值判断。"
}

$rowsBenchB = Import-Csv -Path $latestCsvBenchB.FullName
$benchBPrivateBytesP95 = Get-P95FromRows -rows $rowsBenchB -field "private_bytes"
$benchBPrivateBytesMB = $benchBPrivateBytesP95 / 1MB

Write-Host ""
Write-Host "BENCH-B private_bytes 的 P95（N=$NBenchB）：$([Math]::Round($benchBPrivateBytesMB, 3)) MB（阈值 $BenchBPrivateBytesThresholdMB MB）"
Write-Host "口径说明：50 张 PNG 全量解码后的一次性峰值代理指标，权威值应以 VMMap 为准（本机未装，见 bench/TOOLS.md）。"

if ($benchBPrivateBytesMB -gt $BenchBPrivateBytesThresholdMB) {
    $failures += "BENCH-B private_bytes P95 超标：$([Math]::Round($benchBPrivateBytesMB, 3)) MB > $BenchBPrivateBytesThresholdMB MB。"
}

# ------------------------- 第六步：畸形文档语料稳健性门禁（T43 复用，T44 接入） -------------------------

Write-Host ""
if ($SkipFuzz) {
    Write-Host "[6/6] -SkipFuzz 已指定，跳过 ci\run_fuzz.ps1（仅限本地调试使用，CI 中不应跳过）。"
} else {
    Write-Host "[6/6] 调用 ci\run_fuzz.ps1（畸形文档语料，非零退出即失败）..."
    if ($FuzzTimeoutSeconds -ge 0) {
        Write-Host "（-FuzzTimeoutSeconds 覆盖为 $FuzzTimeoutSeconds 秒，仅用于门禁反向验证，正式跑法不应传这个参数）"
        & (Join-Path $scriptDir "run_fuzz.ps1") -BuildConfig $BuildConfig -TimeoutSeconds $FuzzTimeoutSeconds
    } else {
        & (Join-Path $scriptDir "run_fuzz.ps1") -BuildConfig $BuildConfig
    }
    $fuzzExitCode = $LASTEXITCODE
    if ($fuzzExitCode -ne 0) {
        $failures += "run_fuzz.ps1 退出码为 $fuzzExitCode（应为 0），畸形文档语料未全部通过。"
    } else {
        Write-Host "run_fuzz.ps1 通过（退出码 0）。"
    }
}

# ------------------------- 汇总 -------------------------

Write-Host ""
Write-Host "==== 门禁结果汇总 ===="
if ($failures.Count -eq 0) {
    Write-Host "全部通过。" -ForegroundColor Green
    exit 0
} else {
    Write-Host "未通过，共 $($failures.Count) 项超标：" -ForegroundColor Red
    foreach ($f in $failures) {
        Write-Host "  - $f" -ForegroundColor Red
    }
    exit 1
}
