# Lab3 B 部分交付说明

负责人：严苑毓 2413682。整理与复测日期：2026-10-28。

交付根目录只包含 code 和 report 两个文件夹。B 的中断处理已经实现，报告按老师模板组织，六张测试截图来自本轮实际运行窗口。

```text
code/
  Makefile
  kern/               老师底版、B 正式实现与必要的实验二依赖
  libs/
  tools/              老师原始测试文件
  tests/b/            B 验证脚本与老师底版哈希
report/
  report.md           按模板整理的 B 部分实验报告
  prompt.md           B 的实现、测试与交付规格汇总
  images/             报告引用的六张本轮测试截图
  logs/               编译、串口、验收与截图执行记录
  patches/            B 修改及公共依赖迁移补丁
  capture/            实时测试窗口与运行脚本
  references/         老师实验报告模板
  截图说明.md
  README.md
```

## 阅读与复现

先读 [实验报告](report.md) 和 [提示词汇总](prompt.md)。在 WSL Ubuntu-22.04 中进入 code 文件夹，执行：

```sh
python3 tests/b/verify_b.py
```

脚本将临时支持环境放在交付目录外的 `../.qa/B_2413682`，更新 report/logs，保留正式代码中 A/C 的空位。工具目录以本机安装为准，脚本 PATH 已列出本机路径。

## 实测结果与截图

本轮重新通过源码范围检查、宿主分类/边界、B 文件交叉编译、QEMU 默认十次输出关机、老师原始 TIMER 测试以及覆盖为三次输出后关机。老师 TIMER 输出为 `PASS (40/40, ticks=200)`。结果见 [summary.json](logs/summary.json)，截图采集方式和逐图对应关系见 [截图说明](截图说明.md)。

截图窗口执行新的测试进程，直接显示其 stdout；截图由原生窗口截图工具获取。没有将历史日志重绘为终端图片。OpenSBI 横幅只在实时窗口中略过，完整串口输出保存在 logs/screenshots/。

## 合并说明

- B 修改在 [trap.c](../code/kern/trap/trap.c)：sbi.h、SHUTDOWN_AFTER 配置和 interrupt_handler；C 的前置声明可保留，合并时保留 C 同学的函数实现。
- [B_interrupt.patch](patches/B_interrupt.patch) 只包含 B 修改；[shared_lab2.patch](patches/shared_lab2.patch) 包含实验二 page_init 与 Best-Fit 迁移。保留 Lab3 新增的中断保护，不用实验二整份 pmm.c 覆盖。
- 三份补丁已在老师原始底版上通过 `git apply --check`；C 已修改同一文件时按函数合并，不能直接覆盖其文件。
- SHUTDOWN_AFTER 默认 10 是本次实现约定，单位为统计输出次数；可用编译宏覆盖，组内明确新值后重新验证。

## 当前验证范围

正式 code 中 A 的 RESTORE_ALL/__alltraps 与 C 的 exception_handler 仍由对应成员完成。为了验证 B，脚本只在临时副本中提供 A 入口恢复支持与遇异常即终止的 C 占位函数，后者不是 C 的异常处理实现。

本机 GCC 15.1.0 使用 `-Dsbadaddr=stval` 兼容旧 CSR 名称；测试副本采用实验二已使用的恒等过渡映射，并以 `-kernel bin/ucore.img` 启动 QEMU。公共启动补丁见 [shared_boot_transition.patch](patches/shared_boot_transition.patch)。正式代码未为了测试而修改老师 SAVE_ALL、entry.S、Makefile 或 grade.sh。

本交付完成 B 的测试与截图，未执行全组 make grade，也不宣称全组 100 分。模板中的全组 make grade 唯一结果截图，应在三人正式代码集成后补入小组总报告。
