┌──────────────────────────────────────┐
│                                                                                 │             
│       应该能用的解压缩工具v1.0                                     │
│                                                                                 │
│         ZhangYixin/StupidPenguin                                 │
│                                                                                 │
└──────────────────────────────────────┘
感谢老师点开~请老师将文件放置于linux系统内，点击任意一个目录里的Huffman可执行程序即可运行~
或者打开终端输入./Huffman也可以~感谢老师~

第一个Original Version是只支持自创格式.zyx的个人版，第二个Huffman是支持.zip和.tar文件的正式版~(详情见1190324126-张艺新.pdf)

附录
1190324126-张艺新
├──Original Version/
│    ├── Huffman          # 编译好的可执行文件
│    ├── Makefile         # 编译脚本
│    ├── src/
│    │   ├── main.c       # 入口函数（解析 -h 参数后启动 GUI）
│    │   ├── core.h       # 公共头文件（类型定义、函数声明）
│    │   ├── core.c       # 核心压缩/解压实现（Huffman + libzip + libarchive）
│    │   └── gui.c        # GTK+3 图形界面实现
│     ├── small.txt        # 测试文件1（6.1 MB）
│     └── big.txt          # 测试文件2（325 MB）
├──Huffman/
│    ├── Huffman          # 编译好的可执行文件
│    ├── Makefile         # 编译脚本
│    ├── src/
│    │   ├── main.c       # 入口函数（解析 -h 参数后启动 GUI）
│    │   ├── core.h       # 公共头文件（类型定义、函数声明）
│    │   ├── core.c       # 核心压缩/解压实现（Huffman + libzip + libarchive）  
│    │   └── gui.c        # GTK+3 图形界面实现
│    ├── small.txt        # 测试文件1（6.1 MB）
│    └── big.txt          # 测试文件2（325 MB）
├──1190324126-张艺新.pdf
├──Readme.txt