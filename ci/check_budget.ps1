<#
.SYNOPSIS
    mdvn CI 性能门禁脚本（T16）。

.DESCRIPTION
    依次执行：
    1. 若 Release 构建产物不存在（或指定 -ForceRebuild），先执行 CMake 构建；
    2. 运行 mdvn_tests.exe，退出码非 0 则整体失败；
    3. 调用 bench\run_bench.ps1 做一次暖启动测量（轮数通过 -N 暴露给调用者，
       脚本自身默认沿用文档约定的 20 次；本地手动验证时可传更小的值节省时间）；
    4. 对三项指标做阈值判断：首屏时间（P95）、常驻内存代理指标 private_bytes、
       exe 体积。任一项超标则脚本以非零退出码结束。

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
    暖启动测量轮数，透传给 run_bench.ps1。默认 20（与文档一致）；
    本地手动验证时可以传更小的值（例如 10）节省时间。

.PARAMETER BuildConfig
    CMake 构建配置，默认 Release。

.PARAMETER ForceRebuild
    即使构建产物已存在，也强制重新构建一次。

.PARAMETER FirstPaintP95ThresholdMs
    首屏时间（t_process_to_present_ms）P95 阈值，单位毫秒。
    默认 400（对应 M0 表格"冷启动首屏"上限档；见上方口径说明，这是简化映射）。

.PARAMETER PrivateBytesThresholdMB
    private_bytes（PrivateUsage，常驻内存的自动化代理指标）阈值，单位 MB。
    默认 12（对应 M0 表格"Private Working Set"上限档；权威值应以 VMMap 为准，
    本机未装 VMMap，故用该代理指标先行把关）。

.PARAMETER ExeSizeSoftLimitMB
    exe 体积的宽松安全阈值，单位 MB，默认 5。
    文档原文是"记录基线即可"，不做硬性阈值判断（M3 才门禁 1.5MB）。
    这里额外加一道非常宽松的安全网：当前 exe 实测约 228KB，5MB 已是
    20 倍以上的余量，只用来防止"不小心静态链接了一个很大的库"这种明显异常，
    不会对正常的体积增长产生误报。超过这个阈值也会让脚本失败，但这不是
    M0/M3 文档规定的门槛，只是本脚本作者加的一道保险，请勿与官方阈值混淆。

.EXAMPLE
    powershell -File ci\check_budget.ps1 -N 10
    本地手动验证：跑 10 轮暖启动测量并做门禁判断。
#>

param(
    [int]$N = 20,
    [string]$BuildConfig = "Release",
    [switch]$ForceRebuild,
    [double]$FirstPaintP95ThresholdMs = 400,
    [double]$PrivateBytesThresholdMB = 12,
    [double]$ExeSizeSoftLimitMB = 5
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

Write-Host "==== mdvn CI 性能门禁（T16）===="
Write-Host "当前门禁阈值参照 M0 表格但测量口径与文档严格定义不完全一致，这是已知简化，非最终门禁。"
Write-Host ""

# ------------------------- 第一步：构建（若需要） -------------------------

$needBuild = $ForceRebuild -or (-not (Test-Path $exePath)) -or (-not (Test-Path $testsExePath))
if ($needBuild) {
    Write-Host "[1/4] 未找到构建产物或指定强制重建，开始构建（$BuildConfig）..."
    if (-not (Test-Path $buildDir)) {
        cmake -S $repoRoot -B $buildDir -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败，退出码 $LASTEXITCODE" }
    }
    cmake --build $buildDir --config $BuildConfig
    if ($LASTEXITCODE -ne 0) { throw "CMake 构建失败，退出码 $LASTEXITCODE" }
} else {
    Write-Host "[1/4] 构建产物已存在，跳过构建（传 -ForceRebuild 可强制重建）。"
}

if (-not (Test-Path $exePath)) { throw "构建后仍找不到 mdvn.exe：$exePath" }
if (-not (Test-Path $testsExePath)) { throw "构建后仍找不到 mdvn_tests.exe：$testsExePath" }

# ------------------------- 第二步：单元测试 -------------------------

Write-Host ""
Write-Host "[2/4] 运行 mdvn_tests.exe ..."
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
Write-Host "[3/4] 运行 bench\run_bench.ps1 做暖启动测量（N=$N）..."
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
Write-Host "[4/4] 记录 exe 体积基线..."
$exeSizeBytes = (Get-Item $exePath).Length
$exeSizeMB = $exeSizeBytes / 1MB
Write-Host "mdvn.exe 体积：$([Math]::Round($exeSizeMB, 4)) MB（$exeSizeBytes 字节）"
Write-Host "文档口径：M0 阶段只记录基线，不做硬性门禁（M3 才门禁 1.5MB）。"
Write-Host "本脚本额外加了一道宽松安全网阈值 $ExeSizeSoftLimitMB MB（见脚本头部注释的依据说明），仅用于防止体积失控，不代表官方门槛。"

if ($exeSizeMB -gt $ExeSizeSoftLimitMB) {
    $failures += "exe 体积超过宽松安全网阈值：$([Math]::Round($exeSizeMB, 4)) MB > $ExeSizeSoftLimitMB MB（注意：这不是文档规定的官方门槛，是本脚本额外加的安全网）。"
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
