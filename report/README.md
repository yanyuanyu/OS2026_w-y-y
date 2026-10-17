# Lab2 小组最终合并交付

成员：2413681 王优、2413682 严苑毓、2413636 杨思远。日期：2026-10-17。

先阅读 [总报告](report.md)。九张实验运行截图已按模板整理：编译、QEMU 启动、make grade、自检、边界和 GDB，详见[要求核对](要求核对.md)与[截图说明](截图说明.md)。这是正式合并后的版本；旧的《提交_严苑毓_B》保留为当时的阶段交付，不是本次最终集成报告。

| 内容 | 位置 |
|------|------|
| 可重新构建的完整源码、测试和脚本 | [code/](../code/) |
| 按实验模板六章整理的 Markdown 报告 | [report.md](report.md) |
| 初始化、Best-Fit、Buddy 实现与测试、SLUB 实现与测试六份四段式规格 | [prompt.md](prompt.md) |
| 九张真实运行结果截图 | [images/](images/) |
| 本机重新运行的原始输出、GDB 命令与源码审计 | [logs/](logs/) |
| QEMU 4.1.1 源码调试日志 | [evidence/](evidence/) |
| 本地模板及此前读取的课程手册缓存 | [references/](references/) |

## 本机实测结果

最终验收 PASS；Best-Fit 的 11 项确定性与 30,000 次随机操作，Buddy 的 12,000 次随机操作及 11 类错误路径，三后端启动、Buddy 64/128/256 MiB、物理内存初始化的 7 个边界案例、客户机 GDB 的根叶子和两条 sd 均通过。本地 `make grade` 的 Page Init 与 Best-Fit 检查为 20/20，Buddy、SLUB 和地址翻译各有对应运行记录。

实际运行标识和退出码在 [summary.json](logs/final-review-20261017/acceptance/summary.json)，对应源文件哈希在 [source_hashes.json](logs/final-review-20261017/acceptance/source_hashes.json)。王优的 page_init、严苑毓的 Best-Fit 文件和杨思远的 Buddy/探针文件保持采用版本，老师检查函数保持原样。

## 运行

使用 WSL Ubuntu-22.04，进入本包 `code/`。本机工具位置如下；在其他机器上按实际安装目录调整 PATH：

```sh
export PATH=/home/yyy/compiler-env/toolchains/riscv-elf-toolchains/bin:/opt/qemu/bin:"$PATH"
python3 tools/verify_all.py
```

交互演示：

```sh
make qemu
make PMM_ALGO=firstfit qemu
make PMM_ALGO=buddy qemu
```

退出 QEMU：按 Ctrl+A，松开后按 X。默认 Best-Fit，对应 `bin/ucore.img`；First-Fit、Buddy 分别在 `bin/firstfit/`、`bin/buddy/`。不同后端使用独立对象，切换不会误用另一后端对象。

单独检查：

```sh
bash tests/bestfit/run_host.sh
python3 tools/test_buddy_host.py
python3 tools/test_firstfit_tail.py
python3 tools/verify_c.py
python3 tools/test_slub.py
python3 tools/test_page_init.py
python3 tools/debug_guest.py
make grade
```

客户机调试支持通过环境变量 GDB 指定可执行文件，使用普通 GDB 命令脚本。`verify_all.py` 的版本记录使用 riscv64-unknown-elf 工具命名，本机已经确认存在。

## 合并说明

- 初始化采用王优的 page_init；Best-Fit 保留严苑毓版本；Buddy 采用杨思远重写版。
- First-Fit 两处尾插补 break，有实际旧版失败和修复后通过记录；原检查不变。
- 正式 entry.S 增加过渡恒等映射，保留高地址映射；QEMU 使用 -kernel 进入内核。
- grade 在唯一临时目录注入，按完整测试标记等待并清理自己创建的 QEMU；测试源和评分条件不变。
- Buddy 按完整幂次块分配：3 页实际占 4 页，调用者保存原请求并以 free_pages(base,3) 归还整个块。
- 截图保留真实终端中的命令与执行结果，矩形裁剪后的像素与原图对应区域一致。

## 调试记录与拓展

QEMU 4.1.1 源码调试展示页表分支、软件 TLB 冷热访问和 Bare 对照，对应脚本为 code/tools/build_debug_qemu.py 与 debug_qemu.py。系统与开发工具、目标平台及运行配置统一列于实验报告第二章。

本次成果包括物理内存初始化、First-Fit 分析、Best-Fit、Buddy 和地址翻译调试，SLUB 已完成三后端对象分配、复用与回收验证。三位成员的 AI 工具与模型按组员信息填写为 ChatGPT / GPT-6.1-sol。

## 截图与证据

报告采用九张真实运行结果截图，工作区归档保留原图及历次整理记录。`logs/terminal/` 保存截图时的实际输出，`logs/terminal-detail/` 保存内核与调试明细。裁剪说明和校验见[截图说明](截图说明.md)。

## 报告完整性核对

第四章按手册逐项列出练习与对应内容，保留各成员的实现说明、推导及调试过程。完整源码复测九项通过，报告核对见 logs/comprehensive_review_20261017.json。

## 提示词复核

六份提示词和报告已对照老师《提示词如何撰写》同步整理，详见[提示词核对](提示词核对.md)。

## 课程提交

按助教要求，本次实验对应小组 Git 仓库的 `lab2` 分支。在该分支根目录放置以下两个文件夹：

```text
code/                 完整源码、Makefile 与测试脚本
report/
  report.md           六章实验报告
  prompt.md           本实验六份提示词汇总
  images/             报告引用的九张真实测试截图
  logs/               测试与调试原始记录
  evidence/           组员交付的宿主调试记录
  references/         模板、手册与老师说明
```

本目录中的说明、检查清单和截图辅助脚本均位于 report/ 内。解压最终交付包后，将 code/ 和 report/ 放到小组仓库 lab2 分支根目录；构建与测试在 code/ 的 Makefile 所在目录执行。

SLUB 补充：采用王优交接的 slub.c、slub.h、slub_test.c，当前三后端已重新运行。提示词均作为可复现规格整理；工具和模型按组内确认填写。图 1～8 保留基础开发阶段，新增 SLUB 图为王优交付阶段；当前 63 文件集成版本对应九项验收日志。
