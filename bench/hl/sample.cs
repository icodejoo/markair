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
