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
