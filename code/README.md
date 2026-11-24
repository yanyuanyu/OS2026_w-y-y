# Lab5：用户进程（ABC 完整实现）

成员：杨思远 2413636（A）、王优 2413681（B）、严苑毓 2413682（C 与集成）。

## 构建与运行

在配置 RISC-V 裸机工具链、QEMU 和 GDB 的 Linux/WSL 中执行：

```sh
make -j2
make qemu
make grade
RISCV_ARCH=rv64imac_zicsr_zifencei make test-a
make test-all
python3 tests/divzero/run.py
make dual-gdb
```

默认启动 exit；`make build-hello` 选择 hello，之后通过 QEMU 运行生成的镜像。QEMU 可用 Ctrl+A、X 退出。启动适配器将课程 loader 参数转换为 OpenSBI 的 -kernel 入口；课程评分脚本保留其检查条件。

`tests/a` 为真实 PMM/VMM/fork 模块与故障注入；`tests/integration` 为 13 项用户态集成；`tests/c` 包含系统调用探针、双 GDB 和状态一致性模型。测试结果写入 report/logs，主要方法与结果在 report/report.md 完整说明。

`user/divzero.c` 显式执行 RV64 `divw` 指令，验证除数为零时商为 -1 的体系结构语义。`tests/divzero/run.py` 在 `-O0/-O1/-O2/-Os` 下分别构建、核对反汇编并运行该程序。

实现以 A/B 交付为基础接入 C，包含用户程序启动、系统调用、COW fork、exec/wait/exit/yield/kill、调度及双栈返回。COW 使用 PTE_COW=0x100，缺页接口为 cow_pgfault。
