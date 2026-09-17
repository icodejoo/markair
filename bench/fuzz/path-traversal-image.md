# 图片路径穿越语料

以下图片引用刻意使用路径穿越(`../`)试图跳出文档所在目录:

![尝试读取系统文件](../../../../../../windows/system32/config/SAM)

![尝试读取 hosts 文件](..\..\..\..\Windows\System32\drivers\etc\hosts)

![带查询参数的穿越路径](../../../../secret.txt?evil=1)

![绝对路径穿越](C:/Windows/System32/config/SYSTEM)

正常的相对路径图片作为对照:

![正常相对路径](./images/normal.png)
