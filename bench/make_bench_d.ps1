<#
.SYNOPSIS
    生成 BENCH-D（T73：10 MB 结构真实的超大文档语料）。

.DESCRIPTION
    01-requirements.md §4 的"10 MB 超大文档打开时间 ≤ 1.5 s 且 UI 不卡死"
    这条硬指标此前没有真实结构的语料——bench\fuzz\long-line-10mb.md 是畸形
    语料（10MB 单行无换行），测的是稳健性而不是真实排版性能，不能拿它冒充。

    本脚本把 bench\corpus\ 下已有的 39 份真实 README（M3 裁决 #8②：SOURCES.md
    是元数据记录文件，不是语料，排除在外）按**固定的文件名字典序**反复循环
    拼接，直到总大小落入 10 MB ± 0.5 MB 的目标区间，输出到 bench\BENCH-D.md。

    关键设计点：
    ① **确定性**——拼接顺序固定为文件名字典序循环，不做任何随机化，保证
       重复运行产出字节级相同的文件（用 SHA256 可验证）。
    ② **块类型分布接近真实**——直接整篇拼接原始 README，不打散/不重排其
       内部结构，标题/段落/列表/表格/代码块/链接/脚注这些块类型的分布就是
       39 份真实文档自身的分布，循环拼接不会破坏任何一个块的完整性。
    ③ **不含图片**——按 M3 裁决 #8②的要求，逐行过滤掉图片引用
       （Markdown 语法 `![...](...)` 与裸露的 `<img ...>` HTML 标签），
       避免这条指标变成图片解码性能的测量，与 BENCH-B（图片密集场景）重叠。
    ④ **两篇之间插入一个空行**，避免上一篇末尾与下一篇开头的文本被 Markdown
       解析成同一个块（比如上一篇最后一行是段落文字、下一篇第一行是标题，
       没有空行分隔时某些解析器会把两者粘连）。

    语料本身（BENCH-D.md）按 M3 裁决 #8② 不进 git，只有本脚本进 git；
    复跑前先执行一次本脚本即可重新生成。

.PARAMETER TargetBytes
    目标文件大小（字节），默认 10 MB（10 * 1024 * 1024）。

.PARAMETER ToleranceBytes
    允许的误差（字节），默认 0.5 MB，仅用于生成后自检提示，不影响生成逻辑
    本身（生成逻辑是"整篇拼接到超过目标即停止"，天然只会略微超出，不会
    在目标区间内来回摆动）。

.EXAMPLE
    powershell -File bench\make_bench_d.ps1
    用默认corpus目录和默认目标大小生成 bench\BENCH-D.md。
#>

param(
    [long]$TargetBytes = 10 * 1024 * 1024,
    [long]$ToleranceBytes = 512KB
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$corpusDir = Join-Path $scriptDir "corpus"
$outPath = Join-Path $scriptDir "BENCH-D.md"

if (-not (Test-Path $corpusDir)) {
    throw "找不到语料目录：$corpusDir"
}

# 固定拼接顺序：文件名字典序（Sort-Object 默认序数排序，跨机器/跨次运行结果一致）。
# 排除 SOURCES.md ——它是抓取来源记录，不是真实 README 正文（见该文件自身说明）。
$files = Get-ChildItem -Path $corpusDir -Filter "*.md" |
    Where-Object { $_.Name -ne "SOURCES.md" } |
    Sort-Object Name

if ($files.Count -eq 0) {
    throw "语料目录下没有可用的 .md 文件：$corpusDir"
}

Write-Host "参与拼接的语料文件数：$($files.Count)（固定字典序循环）"

# 过滤图片：Markdown 有三种图片语法，都要剔除，只删图片语法本身，
# 保留同行其余文本，避免误删有效内容：
#   - 行内式  ![alt](url "title")
#   - 引用式  ![alt][ref]（常见于徽章，如 [![NPM Version][img]][link]）
#   - 快捷引用式 ![ref]
#   - 裸露的 <img ...> HTML 标签整段剔除。
function Remove-ImageRefs([string]$text) {
    $text = [regex]::Replace($text, '!\[[^\]]*\]\([^)]*\)', '')
    $text = [regex]::Replace($text, '!\[[^\]]*\]\[[^\]]*\]', '')
    $text = [regex]::Replace($text, '!\[[^\]]*\]', '')
    $text = [regex]::Replace($text, '<img\b[^>]*>', '', 'IgnoreCase')
    return $text
}

# 用 StringBuilder 累积内容，循环遍历文件列表直到达到目标大小，
# 每篇之间插入一个空行防止块粘连。UTF8 无 BOM 输出，与仓库里其余语料一致。
$sb = New-Object System.Text.StringBuilder
$totalBytes = 0L
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$round = 0

while ($totalBytes -lt $TargetBytes) {
    $round++
    foreach ($f in $files) {
        $raw = Get-Content -Raw -Path $f.FullName -Encoding UTF8
        $filtered = Remove-ImageRefs $raw
        if (-not $filtered.EndsWith("`n")) { $filtered += "`n" }
        [void]$sb.Append($filtered)
        [void]$sb.Append("`n")

        $totalBytes = $utf8NoBom.GetByteCount($sb.ToString())
        if ($totalBytes -ge $TargetBytes) { break }
    }
}

$finalText = $sb.ToString()
[System.IO.File]::WriteAllText($outPath, $finalText, $utf8NoBom)

$actualBytes = (Get-Item $outPath).Length
$actualMB = [Math]::Round($actualBytes / 1MB, 3)
$targetMB = [Math]::Round($TargetBytes / 1MB, 3)
$diffMB = [Math]::Round(($actualBytes - $TargetBytes) / 1MB, 3)

Write-Host ""
Write-Host "生成完成：$outPath"
Write-Host "循环拼接轮数：$round（每轮遍历全部 $($files.Count) 份语料）"
Write-Host "实际大小：$actualMB MB（目标 $targetMB MB，偏差 $diffMB MB）"

if ([Math]::Abs($actualBytes - $TargetBytes) -gt $ToleranceBytes) {
    Write-Warning "实际大小偏离目标超过容差（±$([Math]::Round($ToleranceBytes/1MB,3)) MB），仅提示，不阻断生成。"
} else {
    Write-Host "落在目标容差区间内（±$([Math]::Round($ToleranceBytes/1MB,3)) MB）。"
}

$sha = (Get-FileHash -Path $outPath -Algorithm SHA256).Hash
Write-Host "SHA256：$sha（用于验证多次运行是否字节级相同）"
