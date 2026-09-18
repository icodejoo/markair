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
