<#
.SYNOPSIS
    mdvn 文件关联注册表零残留验收脚本（M2 T61）。

.DESCRIPTION
    04-delivery-plan.md 的 M2 验收标准原文要求"卸载后注册表无残留"。本脚本
    落地为脚本层验收（Process Monitor 层与"干净虚拟机"层为人工验收，见
    bench/M2-ASSOC.md，不在本脚本范围内）：

    1. 注册前对 HKCU\Software\Classes 下与关联相关的子树（`mdvn.md` 与
       全部五个扩展名 `.md`/`.markdown`/`.mdown`/`.mkd`/`.mdtext`）拍一份快照；
    2. 调用 `mdvn.exe --register`；
    3. 断言预期键都已写入（1 个 ProgID 子树 + 5 条 OpenWithProgids，一条不少）；
    4. 调用 `mdvn.exe --unregister`；
    5. 逐键断言全部消失，并与步骤 1 的快照做全量 diff，差异必须为空。

    扩展名清单与 ProgID 名硬编码抄自 src/shell/assoc.h 的
    `kAssociatedExtensions` / `kAssocProgId`（唯一定义处在那份 C++ 头文件里，
    本脚本按 T61 要求手抄一份并在此注明"与 assoc.h 同步维护"，不做 C++ 头
    文件的自动解析）。

    脚本可重复运行：每次运行开头都会先做一次"孤儿残留清理"（如果上一次运行
    中途失败，HKCU 里可能已经留了 mdvn.md 或某个 OpenWithProgids 值）——
    清理时只删除本脚本认识的、且值一致的键，不触碰用户系统上其它程序建立的
    同名扩展名键的其它内容，因此不会因为残留而把无关键值判定为"这是我们
    的残留"从而误删。

.PARAMETER ExePath
    mdvn.exe 的路径，默认 build/src/Release/mdvn.exe（相对仓库根目录）。

.PARAMETER SkipBuild
    默认脚本会在 exe 不存在时自动执行一次 Release 构建；传此开关跳过构建，
    exe 不存在则直接失败。
#>

param(
    [string]$ExePath = "",
    [switch]$SkipBuild
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

# ---------------------------------------------------------------------------
# 与 src/shell/assoc.h 同步维护的常量：五个关联扩展名 + 唯一 ProgID 名。
# 若 assoc.h 里的 kAssociatedExtensions / kAssocProgId 有变化，这里要同步改。
# ---------------------------------------------------------------------------
$AssocExtensions = @(".md", ".markdown", ".mdown", ".mkd", ".mdtext")
$ProgId = "mdvn.md"
$ClassesRoot = "HKCU:\Software\Classes"

# ---------------------------------------------------------------------------
# 步骤 0：确保 exe 存在（不存在则构建）。
# ---------------------------------------------------------------------------
if (-not (Test-Path $ExePath)) {
    if ($SkipBuild) {
        Fail "exe 不存在且 -SkipBuild 已指定：$ExePath"
        exit 1
    }
    Info "exe 不存在，执行一次 Release 构建：$ExePath"
    $buildDir = Join-Path $repoRoot "build"
    # 防御:恢复出的陈旧缓存 $buildDir 可能是用其他生成器配置的,理由同
    # ci\check_budget.ps1 同一处改动。
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    if ((Test-Path $cacheFile) -and
        -not (Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config$' -Quiet)) {
        Info "检测到 $buildDir 是用其他生成器配置的(可能是陈旧缓存)，删除后重新配置。"
        Remove-Item -Recurse -Force $buildDir
    }
    if (-not (Test-Path $buildDir)) {
        # 不写死 VS 版本号生成器,改用 Ninja Multi-Config,理由同
        # ci\check_budget.ps1 同一处改动。
        cmake -S $repoRoot -B $buildDir -G "Ninja Multi-Config"
    }
    cmake --build $buildDir --config Release --target mdvn
    if (-not (Test-Path $ExePath)) {
        Fail "构建后仍找不到 exe：$ExePath"
        exit 1
    }
}
Info "使用 exe: $ExePath"

# ---------------------------------------------------------------------------
# 读取本程序相关键的当前状态（用于孤儿清理 + 快照 + 断言）。
# 返回一个 hashtable：
#   ProgId  -> $true/$false（mdvn.md 子树是否存在）
#   <ext>   -> $true/$false（<ext>\OpenWithProgids\mdvn.md 值是否存在）
# ---------------------------------------------------------------------------
function Get-AssocState {
    $state = @{}
    $progIdPath = Join-Path $ClassesRoot $ProgId
    $state["ProgId"] = Test-Path $progIdPath
    foreach ($ext in $AssocExtensions) {
        $owpPath = Join-Path $ClassesRoot "$ext\OpenWithProgids"
        $hasValue = $false
        if (Test-Path $owpPath) {
            $item = Get-Item -Path $owpPath
            # 注意：OpenWithProgids 的值是 REG_NONE 空字节数组，GetValue 返回
            # byte[]。`$v -ne $null` 在 PowerShell 里对数组左操作数会做逐元素
            # 比较（空数组比较结果也是空数组，在 if() 里被当假），必须把
            # $null 放在左边强制走标量比较，否则空字节数组会被误判为"不存在"。
            $value = $item.GetValue($ProgId, $null)
            if ($null -ne $value) {
                $hasValue = $true
            }
        }
        $state[$ext] = $hasValue
    }
    return $state
}

# ---------------------------------------------------------------------------
# 孤儿残留清理：上一次运行若中途失败，可能留下 mdvn.md 或个别 OpenWithProgids
# 值。只清理本程序认识的这些具体键/值，不动其它内容，保证脚本可重复运行。
# ---------------------------------------------------------------------------
function Clear-OrphanResidue {
    $progIdPath = Join-Path $ClassesRoot $ProgId
    if (Test-Path $progIdPath) {
        Info "检测到孤儿残留 $ProgId，先清理"
        Remove-Item -Path $progIdPath -Recurse -Force
    }
    foreach ($ext in $AssocExtensions) {
        $owpPath = Join-Path $ClassesRoot "$ext\OpenWithProgids"
        if (Test-Path $owpPath) {
            $item = Get-Item -Path $owpPath
            $value = $item.GetValue($ProgId, $null)
            if ($null -ne $value) {
                Info "检测到孤儿残留 $ext\OpenWithProgids\$ProgId，先清理"
                Remove-ItemProperty -Path $owpPath -Name $ProgId -Force
            }
        }
    }
}

Info "运行前孤儿残留检查"
Clear-OrphanResidue
$preState = Get-AssocState
foreach ($key in $preState.Keys) {
    if ($preState[$key]) {
        Fail "孤儿清理后仍检测到残留：$key"
    }
}

# ---------------------------------------------------------------------------
# 步骤 1：注册前快照（相关子树的完整键值内容，用于卸载后的全量 diff）。
# 用 reg export 拍快照最贴近"真实注册表内容"，逐扩展名 + ProgID 分别导出，
# 某个键不存在时 reg export 会失败，此时记录为"不存在"的占位内容。
# ---------------------------------------------------------------------------
function Export-Snapshot {
    $lines = New-Object System.Collections.Generic.List[string]
    $targets = @($ProgId) + $AssocExtensions
    foreach ($t in $targets) {
        $regPath = "HKCU\Software\Classes\$t"
        $tmp = New-TemporaryFile
        $exported = $false
        try {
            reg export $regPath $tmp.FullName /y 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) {
                $exported = $true
            }
        } catch {}
        if ($exported) {
            $content = Get-Content -Path $tmp.FullName -Encoding Unicode
            foreach ($l in $content) { $lines.Add("$t|$l") }
        } else {
            $lines.Add("$t|<absent>")
        }
        Remove-Item -Path $tmp.FullName -Force -ErrorAction SilentlyContinue
    }
    return $lines
}

Info "步骤 1：注册前快照"
$snapshotBefore = Export-Snapshot

# ---------------------------------------------------------------------------
# 步骤 2：调用 --register。
# ---------------------------------------------------------------------------
Info "步骤 2：调用 --register"
# 用 Start-Process -Wait 而不是 `& $ExePath`：mdvn 是 WIN32 子系统程序，
# `&` 调用方式下 PowerShell 有时会在子进程真正写完注册表 / 退出之前就
# 拿回控制权（观察到 Test-Path 紧跟着读到的仍是旧状态），Start-Process -Wait
# 能确保子进程完全退出后才继续。
$registerProc = Start-Process -FilePath $ExePath -ArgumentList "--register" -Wait -PassThru
$registerExit = $registerProc.ExitCode
Info "--register 退出码: $registerExit"
if ($registerExit -ne 0) {
    Fail "--register 退出码非 0"
}

# ---------------------------------------------------------------------------
# 步骤 3：断言注册后预期键都在（1 个 ProgID 子树 + 5 条 OpenWithProgids）。
# ---------------------------------------------------------------------------
Info "步骤 3：断言注册后键集合完整"
$afterRegister = Get-AssocState
if ($afterRegister["ProgId"]) {
    Ok "$ProgId 子树已存在"
} else {
    Fail "$ProgId 子树未写入"
}
$owpCount = 0
foreach ($ext in $AssocExtensions) {
    if ($afterRegister[$ext]) {
        $owpCount++
        Ok "$ext\OpenWithProgids\$ProgId 已存在"
    } else {
        Fail "$ext\OpenWithProgids\$ProgId 缺失"
    }
}
if ($owpCount -ne 5) {
    Fail "OpenWithProgids 条目数应为 5，实际 $owpCount"
}

# ---------------------------------------------------------------------------
# 步骤 4：调用 --unregister（无论步骤 3 是否有断言失败都要执行，确保收尾清理）。
# ---------------------------------------------------------------------------
Info "步骤 4：调用 --unregister"
$unregisterProc = Start-Process -FilePath $ExePath -ArgumentList "--unregister" -Wait -PassThru
$unregisterExit = $unregisterProc.ExitCode
Info "--unregister 退出码: $unregisterExit"
if ($unregisterExit -ne 0) {
    Fail "--unregister 退出码非 0"
}

# ---------------------------------------------------------------------------
# 步骤 5：逐键断言全部消失，并与注册前快照做全量 diff。
# ---------------------------------------------------------------------------
Info "步骤 5：断言卸载后键集合清空 + 与注册前快照全量 diff"
$afterUnregister = Get-AssocState
if (-not $afterUnregister["ProgId"]) {
    Ok "$ProgId 子树已消失"
} else {
    Fail "$ProgId 子树卸载后仍存在"
}
foreach ($ext in $AssocExtensions) {
    if (-not $afterUnregister[$ext]) {
        Ok "$ext\OpenWithProgids\$ProgId 已消失"
    } else {
        Fail "$ext\OpenWithProgids\$ProgId 卸载后仍存在"
    }
}

$snapshotAfter = Export-Snapshot
$diff = Compare-Object -ReferenceObject $snapshotBefore -DifferenceObject $snapshotAfter
if ($diff) {
    Fail "卸载后快照与注册前快照存在差异："
    $diff | ForEach-Object { Write-Host "  $($_.SideIndicator) $($_.InputObject)" }
} else {
    Ok "卸载后快照与注册前快照完全一致（全量 diff 为空）"
}

if ($failed) {
    Write-Host "`n验收失败" -ForegroundColor Red
    exit 1
} else {
    Write-Host "`n验收通过：注册表零残留" -ForegroundColor Green
    exit 0
}
