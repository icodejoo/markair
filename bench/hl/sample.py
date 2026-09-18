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
