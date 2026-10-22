#include "core.h"

int run_gui(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    if (argc > 1 && (strcmp(argv[1], "-h") == 0)) {
        printf("应该能用的解压缩工具v1.0\n");
        printf("作者: ZhangYixin/StupidPenguin\n\n");
        printf("用法: 直接运行启动图形界面\n");
        return 0;
    }
    return run_gui(argc, argv);
}
