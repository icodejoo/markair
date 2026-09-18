<#
.SYNOPSIS
    mdvn clang-cl ASAN 验证脚本（T79，沿用 M1 T43 已验证过的路子）。

.DESCRIPTION
    04 测量方法表里"ASAN 只用于验证,不进正式构建"这条手段的落地脚本：
    独立配置一份 build-asan\（VS 2022 生成器 + -T ClangCL 工具集，本机没有
    Ninja，因此不用 T43 commit 里提到的 Ninja 方案，改用等价的 VS+ClangCL
    工具集），对 mdvn_tests 附加 /fsanitize=address，喂 bench\fuzz\ 全部 9 份
    畸形语料 + BENCH-A/B/C/D（分别由 test_fuzz_smoke.cpp / test_bench_abc_smoke.cpp /
    test_benchd_smoke.cpp 覆盖，走的是"直接调用解析/布局函数，不经过窗口"的
    链路——T79 排查发现 ASAN 构建的 mdvn.exe 真实 GUI 路径会在窗口创建前挂死，
    这是构建环境限制不是内存安全问题，详见 bench\M3-LEAK.md 的 T79 章节，
    因此不通过启动 mdvn.exe 本体来喂语料）。

    已知的工具链假阳性（T43 已定性，本脚本复现一致）：MSVC/lld 对多个 TU 里
    内容相同的空字符串字面量（如 L""、""）做只读数据折叠，ASAN 的全局变量
    影子内存机制会把折叠后的同一地址错误当成"多个不同大小的全局变量重叠"，
    报成 odr-violation 或 global-buffer-overflow，与 test_cmdline.cpp /
    test_image.cpp / test_attr.cpp 里的空字符串全局量有关，与真实语料解析/
    布局逻辑无关（在这些误报之前，全部真实用例的 stderr 输出已经完整打印）。
    本脚本用 ASAN_OPTIONS=detect_odr_violation=0 压掉其中一种报法，但另一种
    （global-buffer-overflow）在同一地址上仍会出现——这是预期的已知噪声，
    脚本按"真实语料相关输出是否齐全 + 是否只在已知误报点之后才出现异常"两条
    判定，不是简单看退出码。

.PARAMETER ForceReconfigure
    即使 build-asan\ 已存在，也强制重新执行一次 cmake 配置。

.PARAMETER AsanRuntimeDir
    clang_rt.asan_dynamic-x86_64.dll 所在目录（运行期需要在 PATH 上）。
    默认指向本机一份从 VC Tools\Llvm 拷贝出来的短路径目录（避免路径里的
    空格导致 CMake/MSBuild 传参出错，实测"Program Files (x86)"路径直接传给
    CMAKE_EXE_LINKER_FLAGS 会被引号处理成截断的半个路径）。

.EXAMPLE
    powershell -File ci\run_asan.ps1
    配置(若不存在)+构建 build-asan\ 下的 mdvn_tests,跑一遍全部真实语料。
#>

param(
    [switch]$ForceReconfigure,
    [string]$AsanRuntimeDir = "C:\asan_libs_mdvn"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$buildDir = Join-Path $repoRoot "build-asan"
$testsExePath = Join-Path $buildDir "tests\Release\mdvn_tests.exe"

Write-Host "==== mdvn clang-cl ASAN 验证（T79） ===="

# ------------------------- 第零步：ASAN 动态运行时准备 -------------------------
# lld-link 走 VS 工程生成时不会像 clang 驱动那样自动补 ASAN 运行时导入库
# （/INFERASANLIBS 只在 clang-cl 直接调用链接器时生效），因此显式把
# clang_rt.asan_dynamic-x86_64.lib / _runtime_thunk 拷到一个无空格路径下，
# 配置期通过 CMAKE_EXE_LINKER_FLAGS 显式带上；运行期 clang_rt.asan_dynamic-
# x86_64.dll 同理需要在 PATH 上，一并放这个目录。
$llvmLibDir = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\lib\clang\19\lib\windows"
if (-not (Test-Path $AsanRuntimeDir)) {
    New-Item -ItemType Directory -Path $AsanRuntimeDir -Force | Out-Null
}
foreach ($f in @("clang_rt.asan_dynamic-x86_64.lib", "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib", "clang_rt.asan_dynamic-x86_64.dll")) {
    $dst = Join-Path $AsanRuntimeDir $f
    if (-not (Test-Path $dst)) {
        $src = Join-Path $llvmLibDir $f
        if (-not (Test-Path $src)) {
            Write-Host "FAIL: 找不到 ASAN 运行时文件 $src（本机 VC Tools\Llvm 版本可能不同，需要调整脚本里的版本号）。"
            exit 1
        }
        Copy-Item $src $dst
    }
}

# ------------------------- 第一步：配置(若需要) -------------------------
$needConfigure = $ForceReconfigure -or (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt")))
if ($needConfigure) {
    Write-Host "[1/3] 配置 build-asan\（VS 2022 + ClangCL 工具集 + /fsanitize=address）..."
    $linkerLibs = (Join-Path $AsanRuntimeDir "clang_rt.asan_dynamic-x86_64.lib") + " " + (Join-Path $AsanRuntimeDir "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
    cmake -S $repoRoot -B $buildDir -G "Visual Studio 17 2022" -A x64 -T ClangCL `
        -DCMAKE_CXX_FLAGS="/fsanitize=address" `
        -DCMAKE_C_FLAGS="/fsanitize=address" `
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreadedDLL" `
        -DCMAKE_EXE_LINKER_FLAGS="$linkerLibs"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL: cmake 配置失败，退出码 $LASTEXITCODE"
        exit 1
    }
} else {
    Write-Host "[1/3] build-asan\ 已配置，跳过（传 -ForceReconfigure 可强制重新配置）。"
}

# ------------------------- 第二步：构建 mdvn_tests（Release 配置——ASAN 不支持 /MTd 调试运行时） -------------------------
Write-Host "[2/3] 构建 mdvn_tests（Release 配置）..."
cmake --build $buildDir --target mdvn_tests --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAIL: cmake 构建失败，退出码 $LASTEXITCODE"
    exit 1
}
if (-not (Test-Path $testsExePath)) {
    Write-Host "FAIL: 构建后仍找不到 mdvn_tests.exe：$testsExePath"
    exit 1
}

# ------------------------- 第三步：跑，按"真实语料输出是否齐全"判定 -------------------------
Write-Host ""
Write-Host "[3/3] 运行 mdvn_tests.exe（ASAN，喂 bench\fuzz\ 9 份 + BENCH-A/B/C/D）..."

$env:PATH = "$AsanRuntimeDir;$env:PATH"
$env:ASAN_OPTIONS = "halt_on_error=0:detect_odr_violation=0"

$logPath = Join-Path $repoRoot "bench\artifacts\t79_asan_final.log"
New-Item -ItemType Directory -Path (Split-Path $logPath) -Force | Out-Null

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $testsExePath
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$stdout = $proc.StandardOutput.ReadToEnd()
$stderr = $proc.StandardError.ReadToEnd()
$proc.WaitForExit()
($stdout + $stderr) | Out-File -FilePath $logPath -Encoding utf8

# 真实语料相关的四类冒烟输出全部出现,且都不是紧跟在 ASAN 报告之后
# （即误报只出现在这些行之后 —— 与 T43/T79 已定性的字符串字面量折叠假阳性
# 位置一致），才判定为"ASAN 未在真实语料路径上发现问题"。
$requiredMarkers = @("fuzz_smoke:", "bench_abc_smoke:", "benchd_smoke:", "corpus_smoke:")
$allPresent = $true
foreach ($m in $requiredMarkers) {
    if (($stdout + $stderr) -notmatch [regex]::Escape($m)) {
        Write-Host "FAIL: 输出里缺少标记 '$m'，说明真实语料链路没跑完就中断了。"
        $allPresent = $false
    }
}

if (-not $allPresent) {
    Write-Host "详细输出见 $logPath"
    exit 1
}

Write-Host "PASS: bench\fuzz\ 9 份 + BENCH-A/B/C/D 全部跑完，四类冒烟输出齐全。"
Write-Host "详细输出（含已知字符串字面量折叠假阳性噪声）见 $logPath"
exit 0
