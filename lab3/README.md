# Lab3：中断与异常处理

| 分工 | 姓名 | 学号 |
|---|---|---|
| Trap 入口与上下文恢复 | 严苑毓 | 2413682 |
| 中断与时钟处理 | 杨思远 | 2413636 |
| 异常处理 | 王优 | 2413681 |

- [实验代码](code/)
- [总体报告与要求验收](report/integration-abc/验收报告.md)
- [Trap 入口专项报告](report/A.md)
- [全组实验报告](report/report.md)
- [全组 AI 提示词汇总](report/prompt.md)
- [异常处理专项报告](report/C/C组实验报告.md)
- [本轮日志](report/integration-abc/logs/)

在 WSL/Linux 中进入 lab3/code：

```sh
make -j2
python3 tests/test_exception.py
make grade
make qemu
```

本分支保持根目录 Lab2 内容不变。Lab3 全部生产函数已合入；正式启动路径增加分页过渡恒等映射，QEMU 使用 -kernel，评分测试项目、输入与判分条件未改变。详情见总体报告。

tests/b 与 tests/ab 脚本、report/logs 及 report/integration 为成员交付和前轮集成的历史证据，其 A/C 占位及旧启动映射假设不适用于当前三人完整代码。当前验收请使用上述命令。C 附件原始日志保留在 report/C/测试日志，不冒充本轮结果。

三位成员姓名和学号已按用户确认补齐；班级未提供。提交前按课程要求确认目录结构和完整截图。
