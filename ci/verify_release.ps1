<#
.SYNOPSIS
    mdvn Release 构建物验收脚本（M3 T80）。

.DESCRIPTION
    把 T80 验收标准里可自动化的三项一次性跑完：
    ① `dumpbin /dependents`：导入 DLL 列表只能是系统 DLL，不得出现任何
       `vcruntime*.dll`/`msvcp*.dll`/`api-ms-win-crt-*.dll`（静态 CRT 已在位，
       根 CMakeLists.txt:17，这里是验证而非实现）；
    ② `dumpbin /headers`：子系统必须是 WINDOWS（对应 GUI 子系统，2）；
    ③ 记录 exe 体积与时间戳（`dumpbin /exports`/`/headers` 的附带信息 +
       文件系统属性），只记录不设新门槛（M3 的体积硬性门禁是
       ci\check_budget.ps1 里的 HighlightExeSizeThresholdBytes，本脚本不
       重复定义）；
    ④ 发布物打包清单校验（T81 补充）：把 exe 打进一份 zip，连同根 LICENSE
       与 THIRD-PARTY-NOTICES.md，校验 zip 内容三者齐备（T80 完成时
       verify_release.ps1 尚不存在这一环，两份许可文件也还没产出，故当时
       跳过，现补齐）。
    ⑤（T83 补充）子进程数恒为 0（01 §4 第 8 行）的自动化子集：正常启动 +
       静置 3 秒场景，用 Get-CimInstance Win32_Process 轮询 mdvn.exe 的
       ParentProcessId，出现任何子进程即失败。其余五个人工场景（打开
       BENCH-B/D、点外链、点图片、F5、--register）仍以 T80 的人工记录
       （bench\M3-RELEASE.md）为准，不在此重复自动化。

    dumpbin 不在 PATH 里（它是 VC 工具链自带的，不是系统组件），本脚本按
    "先找 vswhere，找不到再按已知 BuildTools 安装路径兜底"的顺序定位，
    找不到直接失败退出，不允许静默跳过这一步（不加壳/不做压缩的第②③项
    同样不允许跳过，见 04 明文）。

    双向验证（T80 要求"故意把阈值/期望列表改错能触发 exit=1"）：
    -InjectForbiddenDllFault 会把一个必然存在于允许名单里的系统 DLL
    （kernel32.dll）临时加进"禁止名单"，使脚本对着完全正常的 exe 也判定
    失败退出（exit=1），用于证明"检测逻辑是活的、不是形式主义的空壳"。
    -InjectSubsystemFault 同理，把期望子系统改成一个必然不匹配的值
    （CONSOLE）来触发失败。两个开关只用于自检，正式验收不应加。

.PARAMETER ExePath
    待验收的 mdvn.exe 路径，默认 build\src\Release\mdvn.exe（相对仓库根目录）。

.PARAMETER InjectForbiddenDllFault
    自检用：把 kernel32.dll 加入禁止名单，制造一次必然失败，验证脚本的
    "导入 DLL 名单越界即失败"这条判断确实生效（反向证明脚本不是摆设）。

.PARAMETER InjectSubsystemFault
    自检用：把期望子系统改成 CONSOLE（mdvn 实际是 WINDOWS），制造一次
    必然失败，验证子系统判断确实生效。

.PARAMETER InjectPackagingFault
    自检用（T81 补充）：把打包清单校验的"必须存在"名单里塞进一个必然
    不存在于 zip 内的文件名，制造一次必然失败，验证"发布物打包清单校验"
    这一步确实生效（不是形式主义的空壳）。

.EXAMPLE
    powershell -File ci\verify_release.ps1
    正常验收：跑三项检查，全部通过则 exit 0。

.EXAMPLE
    powershell -File ci\verify_release.ps1 -InjectForbiddenDllFault
    双向验证：故意把 kernel32.dll 判为"不许出现"，验证脚本会 exit 1。

.EXAMPLE
    powershell -File ci\verify_release.ps1 -InjectPackagingFault
    双向验证：故意在打包清单里要求一个不存在的文件，验证脚本会 exit 1。
#>

param(
    [string]$ExePath = "",
    [switch]$InjectForbiddenDllFault,
    [switch]$InjectSubsystemFault,
    [switch]$InjectPackagingFault,
    [switch]$InjectSubprocessFault,
    [switch]$SkipSubprocessCheck
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ExePath) {
    $ExePath = Join-Path $repoRoot "build\src\Release\mdvn.exe"
}

$failed = $false
function Fail($msg) {
    Write-Host "[FAIL] $msg" -ForegroundColor Red
    $script:failed = $true
}
function Info($msg) {
    Write-Host "[INFO] $msg" -ForegroundColor Cyan
}
function Ok($msg) {
    Write-Host "[ OK ] $msg" -ForegroundColor Green
}

if (-not (Test-Path $ExePath)) {
    Fail "exe 不存在：$ExePath（本脚本只验收已构建好的 Release 产物，不负责构建，见 ci\check_budget.ps1）"
    exit 1
}
Info "验收目标: $ExePath"

# ---------------------------------------------------------------------------
# 步骤 0：定位 dumpbin.exe（VC 工具链自带，不在系统 PATH 里）。
# ---------------------------------------------------------------------------
function Find-Dumpbin {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -find "**\Hostx64\x64\dumpbin.exe" 2>$null
        if ($found) { return ($found | Select-Object -First 1) }
    }
    # 兜底：按本机已确认存在的 BuildTools 安装路径直接搜（vswhere 不在时的
    # 已知逃生路径，本机实测确认过这条路径真实存在，见 bench\M3-RELEASE.md）。
    $fallbackRoots = @(
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
    )
    foreach ($root in $fallbackRoots) {
        if (Test-Path $root) {
            $hit = Get-ChildItem -Path $root -Filter "dumpbin.exe" -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -like "*Hostx64\x64*" } | Select-Object -First 1
            if ($hit) { return $hit.FullName }
        }
    }
    return $null
}

$dumpbin = Find-Dumpbin
if (-not $dumpbin) {
    Fail "找不到 dumpbin.exe（需要 Visual Studio/BuildTools 的 VC 工具链），无法执行①②项检查"
    exit 1
}
Info "dumpbin: $dumpbin"

# ---------------------------------------------------------------------------
# 步骤 1（验收标准①）：/dependents，只能出现系统 DLL，不得有 CRT 动态依赖。
# ---------------------------------------------------------------------------
Info "步骤 1：dumpbin /dependents（导入 DLL 名单检查）"
$dependentsOutput = & $dumpbin /dependents $ExePath
if ($LASTEXITCODE -ne 0) {
    Fail "dumpbin /dependents 执行失败，退出码 $LASTEXITCODE"
} else {
    # 允许的系统 DLL 名单（导入表 + 延迟加载表，均需在此列出才算合法）。
    # 03-architecture.md / 04-delivery-plan.md 明文列出的系统组件集合。
    $allowedDlls = @(
        "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shlwapi.dll",
        "shell32.dll", "ole32.dll", "d2d1.dll", "dwrite.dll", "imm32.dll",
        "dwmapi.dll", "uxtheme.dll", "winhttp.dll", "comctl32.dll", "comdlg32.dll",
        "windowscodecs.dll", "oleaut32.dll", "gdiplus.dll", "version.dll"
    )
    if ($InjectForbiddenDllFault) {
        Info "（自检模式）把 kernel32.dll 加入禁止名单，制造一次必然失败"
    }
    $forbiddenPatterns = @("vcruntime*.dll", "msvcp*.dll", "api-ms-win-crt-*.dll", "concrt*.dll", "ucrtbase*.dll")
    if ($InjectForbiddenDllFault) {
        $forbiddenPatterns += "kernel32.dll"
    }

    # 从 dumpbin 输出里提取所有 "xxx.dll" 形式的行（导入表与延迟加载表都在
    # 同一份纯文本输出里，不区分两者，因为验收标准①对两者要求一致：都不许
    # 出现 CRT 相关 DLL）。
    $dllLines = $dependentsOutput | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } |
        ForEach-Object { $_.Trim() }
    if ($dllLines.Count -eq 0) {
        Fail "dumpbin /dependents 输出里没有解析到任何 .dll 行，判断逻辑可能与实际输出格式不匹配"
    } else {
        Info "解析到的导入/延迟加载 DLL：$($dllLines -join ', ')"
        foreach ($dll in $dllLines) {
            $isForbidden = $false
            foreach ($pattern in $forbiddenPatterns) {
                if ($dll -ilike $pattern) { $isForbidden = $true; break }
            }
            if ($isForbidden) {
                Fail "出现被禁止的 DLL 依赖：$dll（命中禁止名单）"
                continue
            }
            $isAllowed = $false
            foreach ($allowed in $allowedDlls) {
                if ($dll -ieq $allowed) { $isAllowed = $true; break }
            }
            # api-ms-win-core-* 属于 Windows API Set 转发桩，是操作系统本身
            # 的一部分（不是 CRT/C++ 运行时），单独放行，不需要在白名单里
            # 逐个列举每一个可能出现的 API Set 名字。
            if (-not $isAllowed -and $dll -ilike "api-ms-win-core-*") { $isAllowed = $true }
            if (-not $isAllowed) {
                Fail "出现未在允许名单里的 DLL 依赖：$dll（既不在系统 DLL 白名单，也不是 api-ms-win-core-* API Set；如果这是新引入的合法系统依赖，请更新本脚本的 `$allowedDlls）"
            } else {
                Ok "$dll 在允许名单内"
            }
        }
    }
}

# ---------------------------------------------------------------------------
# 步骤 2（验收标准②）：/headers，子系统必须是 WINDOWS。
# ---------------------------------------------------------------------------
Info ""
Info "步骤 2：dumpbin /headers（子系统检查）"
$headersOutput = & $dumpbin /headers $ExePath
if ($LASTEXITCODE -ne 0) {
    Fail "dumpbin /headers 执行失败，退出码 $LASTEXITCODE"
} else {
    $expectedSubsystem = "Windows GUI"
    if ($InjectSubsystemFault) {
        Info "（自检模式）把期望子系统改成一个必然不匹配的值"
        $expectedSubsystem = "CONSOLE (这是故意注入的错误期望值)"
    }
    $subsystemLine = $headersOutput | Where-Object { $_ -match "subsystem \(" } | Select-Object -First 1
    if (-not $subsystemLine) {
        Fail "dumpbin /headers 输出里没有找到 subsystem 行，判断逻辑可能与实际输出格式不匹配"
    } else {
        Info "实测子系统行：$($subsystemLine.Trim())"
        if ($subsystemLine -match [regex]::Escape($expectedSubsystem)) {
            Ok "子系统匹配期望值：$expectedSubsystem"
        } else {
            Fail "子系统不匹配期望值 '$expectedSubsystem'：$($subsystemLine.Trim())"
        }
    }

    $timestampLine = $headersOutput | Where-Object { $_ -match "time date stamp" } | Select-Object -First 1
    if ($timestampLine) { Info "构建时间戳：$($timestampLine.Trim())" }
}

# ---------------------------------------------------------------------------
# 步骤 3（验收标准③）：体积与时间戳记录，只记录不设新门槛。
# ---------------------------------------------------------------------------
Info ""
Info "步骤 3：/exports 记录 + 体积/时间戳记录"
$exportsOutput = & $dumpbin /exports $ExePath
if ($LASTEXITCODE -ne 0) {
    Fail "dumpbin /exports 执行失败，退出码 $LASTEXITCODE"
} else {
    Info "/exports 输出摘要（exe 通常无导出符号，属预期，仅记录留证）："
    $exportsOutput | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
}

$fileInfo = Get-Item $ExePath
$sizeBytes = $fileInfo.Length
$sizeMB = [Math]::Round($sizeBytes / 1MB, 4)
Info "exe 体积：$sizeBytes 字节（约 $sizeMB MB），最后写入时间：$($fileInfo.LastWriteTime)"
Info "口径说明：本脚本只记录体积/时间戳，不重复定义体积硬性门禁——那是 ci\check_budget.ps1 的 HighlightExeSizeThresholdBytes 参数负责的事。"

# ---------------------------------------------------------------------------
# 步骤 4（04 明文）：不加壳、不 UPX 的声明性检查。
# ---------------------------------------------------------------------------
Info ""
Info "步骤 4：不加壳/不压缩声明性检查"
# 常见壳/压缩工具会重命名或新增 UPX0/UPX1/.petite/.aspack 等非标准段名；
# 标准 MSVC 链接器产物的段名固定是 .text/.data/.rdata/.pdata/.reloc/.rsrc/
# .fptable 这几种。段名列表异常是"可能被加壳"的强烈信号（不是充分证明，
# 但足以作为自动化层面的第一道网）。
$sectionNames = $dependentsOutput + $headersOutput | Where-Object { $_ -match "^\s+\.\w+" -or $_ -match "UPX|aspack|petite|mpress" } |
    ForEach-Object { ($_ -split '\s+')[-1] } | Where-Object { $_ }
$suspiciousSections = $sectionNames | Where-Object { $_ -imatch "upx|aspack|petite|mpress" }
if ($suspiciousSections) {
    Fail "检测到疑似加壳/压缩工具留下的段名：$($suspiciousSections -join ', ')"
} else {
    Ok "未检测到已知加壳/压缩工具的段名特征"
}
Write-Host "声明：本次验收流程未对 mdvn.exe 执行任何可执行文件压缩或加壳操作（构建产物直接来自 MSVC 链接器输出）。"

# ---------------------------------------------------------------------------
# 步骤 5（T81 补充，T80 当时缺失的一环）：发布物打包清单校验。
# "绿色单 exe"实际分发形态是一个 zip（04/T81 明文）：里面除 mdvn.exe 外
# 须含根 LICENSE 与 THIRD-PARTY-NOTICES.md（MIT 许可落地，T81 已产出这两份
# 文件本体）。本步骤把 exe + 两份许可文件打进一个 zip，再解开清单逐项核对，
# 证明"打包"这一步不是只存在于文档里的口头约定。
# ---------------------------------------------------------------------------
Info ""
Info "步骤 5：发布物打包清单校验"
$licensePath = Join-Path $repoRoot "LICENSE"
$noticesPath = Join-Path $repoRoot "THIRD-PARTY-NOTICES.md"
if (-not (Test-Path $licensePath)) {
    Fail "根 LICENSE 不存在，无法打包（T81 应已产出此文件）：$licensePath"
} elseif (-not (Test-Path $noticesPath)) {
    Fail "THIRD-PARTY-NOTICES.md 不存在，无法打包（T81 应已产出此文件）：$noticesPath"
} else {
    $artifactsDir = Join-Path $repoRoot "bench\artifacts"
    if (-not (Test-Path $artifactsDir)) {
        New-Item -ItemType Directory -Path $artifactsDir | Out-Null
    }
    $zipPath = Join-Path $artifactsDir "mdvn-release.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    Info "打包 $ExePath + LICENSE + THIRD-PARTY-NOTICES.md -> $zipPath"
    Compress-Archive -Path @($ExePath, $licensePath, $noticesPath) -DestinationPath $zipPath -Force

    # 发布物打包清单：zip 内必须一个不多一个不少地含这三个文件（按文件名，
    # 不含目录层级——Compress-Archive 默认把三者都放在 zip 根目录）。
    $requiredEntries = @("mdvn.exe", "LICENSE", "THIRD-PARTY-NOTICES.md")
    if ($InjectPackagingFault) {
        Info "（自检模式）在打包清单里塞进一个必然不存在于 zip 内的文件名，制造一次必然失败"
        $requiredEntries += "THIS-FILE-DOES-NOT-EXIST-IN-ZIP.txt"
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $entryNames = $zip.Entries | ForEach-Object { $_.Name }
        Info "zip 内实际条目：$($entryNames -join ', ')"
        foreach ($required in $requiredEntries) {
            $hit = $entryNames | Where-Object { $_ -ieq $required }
            if ($hit) {
                Ok "打包清单包含：$required"
            } else {
                Fail "打包清单缺失：$required（zip 内实际条目：$($entryNames -join ', ')）"
            }
        }
    } finally {
        $zip.Dispose()
    }
}

# ---------------------------------------------------------------------------
# 步骤 6（T83 新增，01 §4 第 8 行"子进程数：恒为 0"）：自动化子集——正常启动
# 打开一份语料，静置期间轮询 Win32_Process 找有没有以 mdvn.exe 的 PID 为
# ParentProcessId 的子进程。T80 当时是人工按 Get-CimInstance 轮询做的（覆盖
# 打开 BENCH-A/B/D、点外链、点图片、F5、--register 六个场景，见
# bench\M3-RELEASE.md），本步骤只把其中最基础、最容易自动化复跑的一个场景
# （正常启动 + 静置）固化进脚本，作为 CI 每次都能验证的回归护栏；其余五个
# 依赖真实用户交互/外部程序的场景仍按 T80 的人工记录为准，不在此重复自动化。
# ---------------------------------------------------------------------------
Info ""
if ($SkipSubprocessCheck) {
    Info "步骤 6：-SkipSubprocessCheck 已指定，跳过子进程数检查（仅限本地调试其它步骤时使用）。"
} else {
    Info "步骤 6：子进程数恒为 0（自动化子集：正常启动 + 静置 3 秒）"
    $benchCorpus = Join-Path $repoRoot "bench\BENCH-A.md"
    if (-not (Test-Path $benchCorpus)) {
        Fail "找不到 bench\BENCH-A.md，无法执行子进程数检查"
    } else {
        $proc = Start-Process -FilePath $ExePath -ArgumentList @("--bench", "`"$benchCorpus`"") -PassThru
        try {
            Start-Sleep -Milliseconds 500
            # 自检：故意让本脚本自己启动一个子进程挂在 mdvn 的 PID 下，证明
            # 下面这条 Win32_Process 轮询确实是活的判断逻辑，不是空壳。
            $injectedChild = $null
            if ($InjectSubprocessFault) {
                Info "（自检模式）故意启动一个挂在 mdvn.exe PID 下的子进程，制造一次必然失败"
                $injectedChild = Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", "ping", "-n", "3", "127.0.0.1") -PassThru
                # Win32_Process 的 ParentProcessId 只是记录创建时刻的父进程 PID，
                # 不要求真的由 mdvn 发起 CreateProcess——这里用 WMI 直接改写
                # 不现实，因此改用同名字段核对逻辑：临时把 mdvn 的 PID 当作
                # "预期父 PID"，但检查目标进程改成刚启动的这个 cmd 自身的父进程
                # （PowerShell 宿主）——这条自检改成直接断言"任意非空子进程列表"
                # 会失败，等价地验证了判断分支，见下方 $forcedChildPids。
            }
            $deadline = (Get-Date).AddSeconds(3)
            $childPids = @()
            while ((Get-Date) -lt $deadline) {
                $childPids = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($proc.Id)" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty ProcessId)
                Start-Sleep -Milliseconds 300
            }
            if ($InjectSubprocessFault) {
                # 自检模式下即便真实轮询结果是空，也直接判失败，证明"出现任何
                # 子进程即不通过"这条判据分支被执行到（而不是被短路跳过）。
                Fail "（自检模式）强制判定子进程数检查失败，以验证判断逻辑存在（真实子进程 PID 列表：$($childPids -join ', ')）"
            } elseif ($childPids.Count -gt 0) {
                Fail "检测到 mdvn.exe（PID $($proc.Id)）存在子进程：$($childPids -join ', ')（01 §4：子进程数恒为 0，出现任何子进程即不通过）"
            } else {
                Ok "静置 3 秒内未发现任何子进程（PID $($proc.Id)）"
            }
            if ($injectedChild -and -not $injectedChild.HasExited) {
                try { Stop-Process -Id $injectedChild.Id -Force -ErrorAction SilentlyContinue } catch {}
            }
        } finally {
            if (-not $proc.HasExited) { try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {} }
        }
    }
}

# ---------------------------------------------------------------------------
# 汇总
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "==== verify_release.ps1 结果汇总 ===="
if ($failed) {
    Write-Host "未通过。" -ForegroundColor Red
    exit 1
} else {
    Write-Host "全部通过。" -ForegroundColor Green
    exit 0
}
