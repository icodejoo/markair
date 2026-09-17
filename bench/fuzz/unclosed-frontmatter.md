---
title: 未闭合的 front matter
description: 这份 YAML front matter 故意没有结尾的 ---
tags:
  - fuzz
  - frontmatter

# 正文本应从这里开始,但 front matter 从未闭合

如果 front matter 跳过逻辑处理不当,这段正文可能被整个吞掉,或者被当成
YAML 的一部分处理。
