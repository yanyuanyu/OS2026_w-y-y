# Lab3：A+B 合并交付

本目录由 Lab2 分支新建 lab3 分支后新增，根目录原 Lab2 代码与报告保持不动。

- [实验代码](code/)
- [A 部分说明](report/A.md)
- [B 原始报告](report/report.md)（严苑毓 2413682）
- [B 原始交付说明](report/README.md)
- [本轮合并验证日志](report/integration/logs/)

## 合并范围

A：RESTORE_ALL、__alltraps（原 E 盘当前未挂载，依据本聊天上一轮已验证实现恢复）。学号 STUDENT_ID_TODO 待本人填写。
B：原附件 interrupt_handler、关机次数配置及其报告、截图与测试证据，保留成员学号和署名。
共享依赖：保留 B 附件已迁移的 page_init 与 Best-Fit 实现。
C：exception_handler 尚未提交，生产代码保留声明与占位，未冒充完成。

## 本轮复测

在 WSL Ubuntu-22.04 中进入本目录：

```sh
python3 code/tests/ab/verify_ab.py
```

集成测试从 B 的测试脚本派生，但源码审计允许 A 的汇编改动；测试镜像直接使用合并后的 A 汇编，不另行替换 A。只在临时副本加入遇异常即终止的 C 测试占位和启动恒等过渡映射，并按本机工具链加入旧 CSR 名称兼容宏。临时文件位于仓库的 .qa 目录，不提交。

B 原始脚本 code/tests/b/verify_b.py 是 B 单独交付的范围审计，原样保留；A 合并后请运行上述 AB 脚本。
历史截图与 B 原始日志来自附件，不表示本轮重新截图；本轮实际运行证据单独存放于 report/integration/logs。尚未进行全组三模块 make grade，完整生产链接仍需 C 实现及正式启动衔接。
