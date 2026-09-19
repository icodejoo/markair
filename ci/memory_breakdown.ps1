<#
.SYNOPSIS
    markair 私有内存构成诊断脚本(纯诊断,不设门禁)。

.DESCRIPTION
    2026-09-19 内存调研的第一条建议落地:排查 private_bytes 超标前先搞清楚
    "钱花在哪",而不是凭直觉去改 Arena/渲染目标之类的代码。
    GetProcessMemoryInfo 的 PrivateUsage 只是一个总数,看不出细分——这个
    脚本用 cdb.exe(Windows SDK 自带的 Debugging Tools,`!address -summary`)
    对一个正在运行的 markair.exe 进程做私有/映像/映射内存的分类汇总,输出到
    控制台,供人工分析,不接入 check_budget.ps1 的门禁判定。

    cdb.exe 是"Debugging Tools for Windows"组件(随 Windows SDK 可选安装,
    不一定每台机器都有;GitHub Actions windows-latest 镜像截至编写时未
    确认预装),找不到就跳过并提示,不算失败——这是诊断工具，不是验收项。

.PARAMETER ExePath
    要诊断的 markair.exe 路径，默认 build\src\Release\markair.exe（相对仓库根目录）。

.PARAMETER BenchFile
    启动时打开的文档，默认 bench\EMPTY.md（空文档场景，对应 01§4 第4行
    的常驻内存门禁排查对象）。

.PARAMETER SettleSeconds
    进程启动后静置等待秒数，让 D2D/DWrite 等惰性初始化走完再拍快照。
    默认 3。

.EXAMPLE
    powershell -File ci\memory_breakdown.ps1
    诊断默认 Release exe 打开空文档时的私有内存构成。

.EXAMPLE
    powershell -File ci\memory_breakdown.ps1 -BenchFile bench\BENCH-A.md
    换一份语料看构成差异。
#>

param(
    [string]$ExePath = "",
    [string]$BenchFile = "",
    [int]$SettleSeconds = 3
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ExePath) {
    $ExePath = Join-Path $repoRoot "build\src\Release\markair.exe"
}
if (-not $BenchFile) {
    $BenchFile = Join-Path $repoRoot "bench\EMPTY.md"
}

function Info($msg) { Write-Host "[INFO] $msg" }
function Warn($msg) { Write-Host "[WARN] $msg" -ForegroundColor Yellow }

if (-not (Test-Path $ExePath)) {
    Warn "找不到 exe：$ExePath，先构建 Release 再跑本脚本。"
    exit 0
}
if (-not (Test-Path $BenchFile)) {
    Warn "找不到语料：$BenchFile。"
    exit 0
}

# 定位 cdb.exe：先试传统 Windows SDK "Debugging Tools for Windows" 安装
# 路径，再试 WinDbg Preview(MSIX 包，`winget install Microsoft.WinDbg`
# 装的就是它，同样内置 cdb.exe，本机 2026-09-19 验证过可用)的安装目录
# ——都找不到就说明本机/CI runner 没装这个可选组件，直接跳过，不算失败。
function Find-Cdb {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe",
        "$env:ProgramFiles\Windows Kits\10\Debuggers\x64\cdb.exe",
        "${env:ProgramFiles(x86)}\Windows Kits\11\Debuggers\x64\cdb.exe",
        "$env:ProgramFiles\Windows Kits\11\Debuggers\x64\cdb.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }

    # WinDbg Preview 是按版本号命名子目录的 MSIX 包，路径里的版本号会变,
    # 先按包名通配符缩小到具体子目录(WindowsApps 下每个已装应用一个顶层
    # 目录,不递归),再直接拼 amd64\cdb.exe 的确定路径——不对整个 WindowsApps
    # 做 -Recurse 全盘扫描:里面每个 MSIX 包目录都有独立 ACL,大范围递归
    # 既慢又会因权限不足产生大量噪音(即便 -ErrorAction SilentlyContinue
    # 吞掉了异常,枚举本身仍要遍历所有包目录)。
    $windbgRoot = "$env:ProgramFiles\WindowsApps"
    if (Test-Path $windbgRoot) {
        # 同一个 WinDbg Preview 版本会有多条包目录(如 "_neutral_" 资源包、
        # "_x64_" 实际二进制包)，只有 "_x64_" 那条下面才有 amd64\cdb.exe，
        # 不能只取第一条就判定"找不到"——逐条试,第一条真正命中的才返回。
        $pkgDirs = Get-ChildItem -Path $windbgRoot -Directory -Filter "Microsoft.WinDbg_*" `
            -ErrorAction SilentlyContinue
        foreach ($pkgDir in $pkgDirs) {
            $candidate = Join-Path $pkgDir.FullName "amd64\cdb.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

$cdb = Find-Cdb
if (-not $cdb) {
    Warn "未找到 cdb.exe(Windows SDK 的 Debugging Tools for Windows，可选组件，需要另行安装)。"
    Warn "跳过内存构成诊断——这不影响 check_budget.ps1 的门禁判定，只是拿不到细分数据。"
    exit 0
}
Info "cdb: $cdb"

Info "启动 $ExePath $BenchFile ..."
$proc = Start-Process -FilePath $ExePath -ArgumentList "`"$BenchFile`"" -PassThru
try {
    Start-Sleep -Seconds $SettleSeconds
    if ($proc.HasExited) {
        Warn "进程静置期间已退出，无法诊断。"
        exit 0
    }

    Info "对 PID $($proc.Id) 跑 !address -summary(私有/映像/映射内存分类汇总)..."
    # -pv：附加到运行中的进程但不挂起；-c：附加后执行的命令，!address -summary
    # 打印按 Usage 分类(Heap/Stack/Image/MappedFile/Private 等)的汇总表，
    # q 结束调试会话、detach 掉目标进程(不终止它)。
    & $cdb -pv -p $proc.Id -c "!address -summary;q" 2>&1 | ForEach-Object { Write-Host $_ }
} finally {
    if (-not $proc.HasExited) {
        try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
}
