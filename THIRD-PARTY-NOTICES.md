# 第三方组件声明

markair 自身代码使用根目录 [LICENSE](LICENSE)（MIT）授权。以下是 markair 代码之外、随本项目一起分发的第三方组件清单。

## md4c

- 项目名：md4c
- 上游地址：https://github.com/mity/md4c
- 许可类型：MIT
- 版本：commit `b3c6223903c1df483cef926ba347e531248f0b92`（2026-09-14 23:17:03 +0200），vendored 于 2026-09-16。以上信息逐字取自 [third_party/md4c/VERSION.txt](third_party/md4c/VERSION.txt)。
- 使用范围：仅拷贝核心解析器 `src/md4c.c`、`src/md4c.h`，未使用 md4c-html。
- 许可全文：与上游一致，收录于 [third_party/md4c/LICENSE](third_party/md4c/LICENSE)，全文引用如下。

```
The MIT License (MIT)

Copyright © 2016-2026 Martin Mitáš

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the “Software”),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
```

## lunasvg（含 plutovg）

- 项目名：lunasvg
- 上游地址：https://github.com/sammycage/lunasvg
- 许可类型：MIT
- 版本：v3.5.0，vendored 于 2026-09-19。
- 使用范围：SVG 图片离线栅格化（M3 新增），替换此前"SVG 判不支持"的占位。
- 许可全文：与上游一致，收录于 [third_party/lunasvg/LICENSE](third_party/lunasvg/LICENSE)，全文引用如下。

```
MIT License

Copyright (c) 2020-2025 Samuel Ugochukwu <sammycageagle@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

lunasvg 内嵌 plutovg（2D 光栅化后端，同一上游作者，同 MIT 许可，收录于
[third_party/lunasvg/plutovg/LICENSE](third_party/lunasvg/plutovg/LICENSE)，
全文与上方一致，不重复引用）。plutovg 自身又捆绑了两份第三方代码，均随
plutovg 源码树一并 vendored：

- **stb_truetype.h / stb_image.h**（Sean Barrett，public domain / MIT 双许可，
  见文件头注释）：字体轮廓解析与位图图片解码，markair 只用到 plutovg 的路径
  填充/光栅化部分，这两个头文件随 plutovg 编译单元一起进构建，未单独调用。
- **plutovg-ft-\*.c**（源自 FreeType 的光栅化/描边算法，FreeType License，
  全文收录于 [third_party/lunasvg/plutovg/source/FTL.TXT](third_party/lunasvg/plutovg/source/FTL.TXT)）。
