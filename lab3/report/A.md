# A 部分：Trap 入口与上下文恢复

负责人：严苑毓，学号 2413682。

## Spec

保持 SAVE_ALL、struct trapframe 和 trap() 接口不变。frame 共 36 个 REGBYTES：0–31 为通用寄存器，32 为 sstatus，33 为 sepc，34 为 badvaddr，35 为 cause。RV64 总大小 288 字节，保持调用栈 16 字节对齐。

RESTORE_ALL 先恢复 sstatus、sepc，再恢复 x1、x3–x31，最后由第 2 槽恢复旧 sp；x0 固定为零无需加载。scause 与 badvaddr 为诊断信息，无需作为返回状态写回。__alltraps 保存现场后以 a0=sp 调用 trap()，返回后顺序进入 __trapret 执行恢复与 sret。EPC 推进由 C 处理函数决定，汇编不统一跳过指令；返回特权级遵循保存的 SPP，不强制返回用户态。

## Prompt

请以现有 SAVE_ALL 和 trapframe 为布局依据，只补全 trapentry.S 的 RESTORE_ALL 与 __alltraps。使用 LOAD、REGBYTES；临时寄存器使用后恢复其原值，sp 最后恢复。以 a0 传递当前 frame，jal trap 后进入既有返回路径。不修改 SAVE_ALL、trapframe、C 层中断异常策略，不强制修改 SPP。保留 lab3 标记，学号填写 2413682。

## 来源与测试边界

上一轮聊天在 2026-10-28 已对本实现进行 2/4 字节断点隔离 QEMU 验证；由于原 E 盘当前未挂载，原目录与原日志未迁移，本轮按聊天中留存的实现恢复，不将历史结果作为新测试。

本轮实际测试见 integration/logs：使用合并后的 A 汇编进行 B 的持续时钟与重复 Trap 返回测试。C 模块保留未实现状态，断点/非法指令生产恢复逻辑等待 C 合入。未执行全组三模块评分。

## 核心知识点

硬件保存 Trap 相关 CSR，软件保存通用寄存器；Trap frame 连接硬件现场与 C 层处理。调用约定要求 a0 传参及栈对齐。Trap 返回与进程调度不同，本模块尚不包含用户栈切换、多核同步、浮点/向量上下文等机制。
