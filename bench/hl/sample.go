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
