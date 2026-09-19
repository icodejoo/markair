<#
.SYNOPSIS
    mdvn 畸形文档语料稳健性门禁脚本（T43）。

.DESCRIPTION
    对 bench\fuzz\ 下的全部畸形/恶意语料跑一遍"打开 -> 解析 -> 布局"链路
    （具体断言见 tests\test_fuzz_smoke.cpp：单文件处理 <=3s、
    kMaxNestingDepth/kMaxDocumentNodeCount 按预期触发/不触发、不崩溃）。

    这里不单独编译一个只含 fuzz 用例的可执行文件——mdvn_tests.exe 本来就是
    唯一的测试目标（tests\CMakeLists.txt 的注释里明确"MDVN_TEST 的全局构造
    函数注册手法只允许存在于这里"，没有做用例过滤的命令行接口），所以直接
    跑整个 mdvn_tests.exe：
      - 任何一个用例（含 test_fuzz_smoke.cpp 的用例）失败/崩溃/断言失败，
        进程退出码非 0，脚本原样透传该非零退出码；
      - 全部通过则退出码 0。
    这与 T43 验收线"任何一个失败（崩溃/超时/断言失败）则脚本以非零码退出;
    全部通过则退出码 0"完全一致。

    接口设计上刻意保持和 ci\check_budget.ps1 同一套参数命名/风格
    （-BuildConfig / -ForceRebuild），供 T44 直接调用复用：T44 只需要
    "调用本脚本、检查退出码"这一件事，不需要关心内部怎么构建/怎么跑。

.PARAMETER BuildConfig
    CMake 构建配置，默认 Release。

.PARAMETER ForceRebuild
    即使构建产物已存在，也强制重新构建一次 mdvn_tests 目标。

.PARAMETER TimeoutSeconds
    mdvn_tests.exe 整体运行超时秒数（防御性兜底：正常情况下全部语料
    应在几秒内跑完，若某份语料真的卡死，不能让 CI 无限挂起）。默认 60，
    对应验收线"单文件 <=3s"乘以语料数量再留出充分余量。

.EXAMPLE
    powershell -File ci\run_fuzz.ps1
    用 Release 构建跑一遍全部畸形语料，退出码非 0 即失败。

.EXAMPLE
    powershell -File ci\run_fuzz.ps1 -BuildConfig Debug -ForceRebuild
    强制用 Debug 配置重新构建后再跑。
#>

param(
    [string]$BuildConfig = "Release",
    [switch]$ForceRebuild,
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"

# 脚本自身在 ci\ 目录下，仓库根目录是其上一级。
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

$buildDir = Join-Path $repoRoot "build"
$testsExePath = Join-Path $buildDir "tests\$BuildConfig\mdvn_tests.exe"
$fuzzDir = Join-Path $repoRoot "bench\fuzz"

Write-Host "==== mdvn 畸形文档语料稳健性门禁（T43） ===="

if (-not (Test-Path $fuzzDir)) {
    Write-Host "FAIL: 找不到语料目录 $fuzzDir"
    exit 1
}
$fuzzFileCount = (Get-ChildItem -Path $fuzzDir -Filter "*.md").Count
Write-Host "语料目录：$fuzzDir（$fuzzFileCount 份 *.md）"

# ------------------------- 第一步：构建（若需要） -------------------------

$needBuild = $ForceRebuild -or (-not (Test-Path $testsExePath))
if ($needBuild) {
    Write-Host "[1/2] 未找到 mdvn_tests.exe 或指定强制重建，开始构建（$BuildConfig）..."
    # 防御:恢复出的陈旧缓存 $buildDir 可能是用其他生成器配置的,理由同
    # ci\check_budget.ps1 同一处改动。
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    if ((Test-Path $cacheFile) -and
        -not (Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config$' -Quiet)) {
        Write-Host "[1/2] 检测到 $buildDir 是用其他生成器配置的(可能是陈旧缓存)，删除后重新配置。"
        Remove-Item -Recurse -Force $buildDir
    }
    if (-not (Test-Path $buildDir)) {
        # 不写死 VS 版本号生成器,改用 Ninja Multi-Config,理由同
        # ci\check_budget.ps1 同一处改动。
        cmake -S $repoRoot -B $buildDir -G "Ninja Multi-Config"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: CMake 配置失败，退出码 $LASTEXITCODE"
            exit 1
        }
    }
    cmake --build $buildDir --target mdvn_tests --config $BuildConfig
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: CMake 构建失败，退出码 $LASTEXITCODE"
        exit 1
    }
} else {
    Write-Host "[1/2] mdvn_tests.exe 已存在，跳过构建（传 -ForceRebuild 可强制重建）。"
}

if (-not (Test-Path $testsExePath)) {
    Write-Host "FAIL: 构建后仍找不到 mdvn_tests.exe：$testsExePath"
    exit 1
}

# ------------------------- 第二步：跑测试，判定退出码 -------------------------

Write-Host ""
Write-Host "[2/2] 运行 mdvn_tests.exe（含 test_fuzz_smoke.cpp 的畸形语料用例）..."

# 与 check_budget.ps1 同样的原因：PowerShell 5.1 下 $ErrorActionPreference
# = "Stop" 会把原生程序写 stderr 的行为包装成终止性 ErrorRecord，即便退出码
# 是 0（mdvn_tests 的逐用例耗时/截断状态汇总走的就是 stderr）。这里用后台
# 进程 + 超时的方式跑，同时避免上述 PowerShell 行为误判，并给"卡死"兜底。
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $testsExePath
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $false
$psi.RedirectStandardError = $false
$proc = [System.Diagnostics.Process]::Start($psi)

$finished = $proc.WaitForExit($TimeoutSeconds * 1000)
if (-not $finished) {
    Write-Host "FAIL: mdvn_tests.exe 超过 $TimeoutSeconds 秒未退出，判定为卡死，强制结束进程。"
    try { $proc.Kill() } catch {}
    exit 1
}

$exitCode = $proc.ExitCode
if ($exitCode -ne 0) {
    Write-Host "FAIL: mdvn_tests.exe 退出码为 $exitCode（应为 0）。"
    exit $exitCode
}

Write-Host ""
Write-Host "PASS: 全部测试通过（含 $fuzzFileCount 份 bench\fuzz\ 畸形语料），退出码 0。"
exit 0
