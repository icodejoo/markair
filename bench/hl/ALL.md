# ALL：高亮语料合集（T67）

> 把 `bench/hl/` 下 11 份单语言样例文件用围栏代码块拼成一份 md，围栏语言标记
> 逐一对照 `src/hl/languages.cpp` 里 `ResolveLanguageId` 的别名表选取，
> 确保每个标记都能被正确解析为对应的 `LanguageId`（而不是落到 `kLanguageNone`）。

## 1. C —— `sample.c`

```c
#include <stdio.h>

// 计算两个整数之和(T67 语料样例,含中文注释)
int add(int a, int b) {
    return a + b;
}

int main(void) {
    // C 没有原始字符串,这里用普通字符串测中文字符串字面量
    const char* msg = "你好,世界";
    printf("%s: %d\n", msg, add(1, 2));
    return 0;
}

// ---- 故意示例:以下是未闭合的块注释,单独放在文件末尾一节,----
// ---- 用于验证词法器遇到未闭合 /* 时不会把前面的正文也吞掉 ----
/* 这是故意留下的未闭合块注释,一直到文件结束都不会有配对的结束符
```

## 2. C++ —— `sample.cpp`

```cpp
#include <string>
#include <iostream>

// 用原始字符串存放正则表达式模式,避免反斜杠转义(T67 语料样例)
const char* kPattern = R"(\d+\.\d+)";

// 简单的加法模板函数,含中文注释
template <typename T>
T Add(T a, T b) {
    return a + b;
}

int main() {
    std::string greeting = "你好,C++ 世界";
    std::cout << greeting << " " << kPattern << " " << Add(1, 2) << "\n";
    return 0;
}

// ---- 故意示例:以下是未闭合的块注释,单独放在文件末尾一节 ----
/* 这是故意留下的未闭合块注释,验证词法器不会把上面的正文吞掉
```

## 3. C# —— `sample.cs`

```cs
using System;

// C# 样例:插值字符串与逐字字符串的词法陷阱(T67 语料)
class Greeter
{
    // 打印中文问候,含插值字符串
    public static void Greet(string name)
    {
        string msg = $"你好,{name}!欢迎使用 mdvn";
        Console.WriteLine(msg);
    }

    static void Main()
    {
        // 逐字字符串:反斜杠不转义,常用于 Windows 路径
        string path = @"C:\Users\测试\文档";
        Greet("世界");
        Console.WriteLine(path);

        // 逐字插值字符串组合(更极端的陷阱)
        string combo = $@"路径是: {path}\结束";
        Console.WriteLine(combo);
    }
}
```

## 4. Java —— `sample.java`

```java
// Java 样例:文本块(Text Block)的词法陷阱(T67 语料)
public class Greeter {
    // 用文本块存放一段多行 JSON 说明,含中文注释
    static final String DOC = """
            这是一个文本块示例,
            内部可以直接写引号 "不需要转义",
            也可以写中文字符串。
            """;

    // 计算阶乘,验证普通标识符与关键字的边界
    static long factorial(int n) {
        long result = 1;
        for (int i = 2; i <= n; i++) {
            result *= i;
        }
        return result;
    }

    public static void main(String[] args) {
        System.out.println("你好,Java 世界");
        System.out.println(DOC);
        System.out.println("5! = " + factorial(5));
    }
}
```

## 5. TypeScript —— `sample.ts`

```ts
// TS 样例:模板字符串与正则字面量的词法陷阱(T67 语料)
interface Greeting {
    name: string;
    times: number;
}

// 用模板字符串拼接中文问候语,含表达式插值
function greet(g: Greeting): string {
    return `你好,${g.name}!这是第 ${g.times} 次问候`;
}

// 正则字面量:内部含转义斜杠 /a\/b/,极易与除号混淆
const pattern: RegExp = /a\/b/;

// 除法运算,和上面的正则字面量在词法上容易混淆
function divide(a: number, b: number): number {
    return a / b;
}

console.log(greet({ name: "世界", times: 3 }));
console.log(pattern.test("a/b"), divide(10, 2));
```

## 6. Python —— `sample.py`

```py
# Python 样例:三引号字符串与 f-string 的词法陷阱(T67 语料)

def greet(name: str, times: int) -> str:
    """三引号文档字符串,内部可以直接写单引号 'test' 不用转义。"""
    return f"你好,{name}!这是第 {times} 次问候"


# 三引号字符串常用来存多行中文说明
NOTE = '''
这是一段三引号字符串,
里面写中文注释测试非 ASCII 标识符处理,
也包含 "双引号" 混用。
'''

if __name__ == "__main__":
    print(greet("世界", 2))
    print(NOTE)
```

## 7. Go —— `sample.go`

```go
package main

import "fmt"

// 反引号原始字符串的词法陷阱(T67 语料):内部反斜杠不转义
const pattern = `C:\Users\测试\路径`

// 打印中文问候,含中文注释
func greet(name string) string {
	return fmt.Sprintf("你好,%s!欢迎使用 mdvn", name)
}

func main() {
	fmt.Println(greet("世界"))
	fmt.Println(pattern)

	// 多行反引号原始字符串,常用于 SQL/正则等场景
	sql := `SELECT *
FROM 用户表
WHERE 名字 = '张三'`
	fmt.Println(sql)
}
```

## 8. Rust —— `sample.rs`

```rs
// Rust 样例:r#"..."# 原始字符串与生命周期标注的词法陷阱(T67 语料)
// 'a 生命周期极易被误判成字符字面量的开头,这是 Rust 高亮最经典的坑。

// 带生命周期标注的结构体,持有一个字符串切片引用
struct Greeter<'a> {
    name: &'a str,
}

impl<'a> Greeter<'a> {
    // 构造函数,返回带生命周期的实例
    fn new(name: &'a str) -> Greeter<'a> {
        Greeter { name }
    }

    // 生成中文问候语
    fn greet(&self) -> String {
        format!("你好,{}!欢迎使用 mdvn", self.name)
    }
}

fn main() {
    // r#"..."# 原始字符串,内部可以直接写双引号不用转义
    let pattern = r#"路径示例: "C:\Users\测试""#;
    let g = Greeter::new("世界");
    println!("{}", g.greet());
    println!("{}", pattern);

    // 真正的字符字面量,和生命周期标注 'a 的写法只差一个字符
    let ch: char = 'x';
    println!("{}", ch);
}
```

## 9. JSON —— `sample.json`

```json
{
  "_comment": "JSON 样例:转义与 Unicode 码点的词法陷阱(T67 语料)",
  "name": "你好世界",
  "escaped": "换行\n制表符\t引号\"反斜杠\\",
  "unicode_escape": "\u4f60\u597d",
  "mixed": "中文和 \u0041\u0042 混合",
  "count": 3,
  "enabled": true,
  "empty": null,
  "list": ["苹果", "香蕉", "橙子"]
}
```

## 10. YAML —— `sample.yaml`

```yaml
# YAML 样例:多行标量与锚点的词法陷阱(T67 语料,含中文注释)

# 默认配置,用锚点定义,供下方引用
默认配置: &默认值
  超时: 30
  重试次数: 3

用户A:
  <<: *默认值
  名字: 张三

# 折叠标量(> ):折行会被替换为空格,常用于长句子
简介: >
  这是一段很长的中文简介,
  跨越多行,但最终会被折叠成一行,
  用来测试折叠标量的词法处理。

# 块标量(| ):保留换行,常用于多行脚本或诗句
脚本: |
  echo "你好,世界"
  echo "第二行,保留换行"
```

## 11. Shell —— `sample.sh`

```sh
#!/bin/bash
# Shell 样例:命令替换 $(...) 与 heredoc 的词法陷阱(T67 语料)

# 用命令替换获取当前日期,含中文注释
今天=$(date +%Y-%m-%d)
echo "今天是: $今天"

# heredoc:内部可以直接写引号和变量,不会被当作命令解析
cat <<EOF
你好,世界!
今天的日期是 $今天,
这一行有一个 "双引号" 和一个 '单引号'。
EOF

# 单引号字符串是字面量,不做变量展开,验证与双引号的区别
问候='你好,$今天 不会被展开'
echo "$问候"
```
