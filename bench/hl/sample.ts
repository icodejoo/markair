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
