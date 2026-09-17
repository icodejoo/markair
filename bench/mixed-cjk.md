# 中英混排视觉回归语料（T17）

> 本文件用于人工视觉比对断行位置，覆盖中英文混排断行、长 URL、长代码行、
> 嵌套列表缩进四类场景。内容不追求篇幅，追求场景覆盖。

## 一、中英文混排断行

在日常写作中，中文和英文经常混杂在一起出现，比如我们在讨论 DirectWrite 的
`DWRITE_WORD_WRAPPING` 参数时，常常会连续出现像 zh-cn locale、UTF-16、
BiDi algorithm 这样的英文术语，中间穿插逗号、句号等中文标点。这里连续放一段
比较长的混排文字，用来观察窄窗口下断行会不会在英文单词中间硬切、会不会在
中文字与英文单词之间留出不必要的空隙，以及标点符号会不会被孤立地留在行首。

这是另一段偏日常口语化的中英混排：今天 review 了一下 PR，发现 reviewer 对
performance budget 这块提了几个 follow-up，主要集中在 warm start 和 cold
start 两种场景下 PrivateUsage 的差异，建议我们把 benchmark 脚本的 threshold
配置成可以按 profile 切换的形式，方便以后跑 CI gate 的时候动态调整。

中文单字与英文单字符交替出现时也是断行的高风险点，例如：a和b和c和d和e和f和
g和h和i和j和k和l和m和n和o和p和q和r和s这样连续的模式，用来检查逐字符换行是否
出现异常空格或错位。

## 二、超长不换行的 URL

下面这一行是一个很长的 URL，不应该在中间被断开插入空格，理想情况下要么整体
换到下一行，要么在行内溢出（取决于实现策略），但不能把 URL 字符拆散：

https://learn.microsoft.com/zh-cn/windows/win32/directwrite/direct-write-portal?some=query&another=parameter&yet_another_extremely_long_query_parameter_name=1234567890abcdefghijklmnopqrstuvwxyz

再来一行夹在中文句子中间的长 URL：详细的接口文档可以参考这里
https://example.com/docs/api/v2/reference/very/deep/nested/path/segment/that/keeps/going/and/going/without/any/spaces/or/hyphens/to/break/on 请仔细阅读。

## 三、代码块中不换行的长代码行

下面代码块里第二行是一整行很长的代码，不应该被自动换行，应该保持原样（横向
溢出或出现横向滚动条都可以，但不能插入换行）：

```cpp
// 这是一行正常长度的注释，用来对比下一行的长度。
constexpr wchar_t kVeryLongIdentifierNameForTestingNoWrapBehaviorInCodeBlocksAcrossManyManyCharacters[] = L"用来测试代码块中超长一行是否被错误地自动换行，理论上代码块内部不应该做自动换行处理";
void ThisIsAnotherVeryLongFunctionSignatureUsedPurelyForVisualRegressionTesting(int firstParameterWithALongName, int secondParameterWithAnEvenLongerNameThanTheFirstOne, const wchar_t* thirdParameterPointerToWideString);
```

## 四、嵌套 2~3 层的列表缩进

- 第一层列表项 A
  - 第二层列表项 A-1
    - 第三层列表项 A-1-a
    - 第三层列表项 A-1-b（这里也混一点英文 inline code 试试 `foo_bar()`）
  - 第二层列表项 A-2
- 第一层列表项 B
  - 第二层列表项 B-1
    - 第三层列表项 B-1-a：一段中英文混排文字 mixed with English words 用来
      同时检查缩进对齐和断行位置是否正确
  - 第二层列表项 B-2
- 第一层列表项 C

有序列表版本：

1. 第一层有序项一
   1. 第二层有序项一之一
      1. 第三层有序项一之一之一
   2. 第二层有序项一之二
2. 第一层有序项二
   1. 第二层有序项二之一
3. 第一层有序项三

## 五、收尾

以上四类场景可以在不同窗口宽度下反复缩放窗口来人工比对断行位置，重点关注：
中英文交界处是否有多余空格、URL 与代码行是否被错误折断、嵌套列表的缩进是否
随层级正确递增。
