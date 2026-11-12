# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|---|---|
| 实验名称 | Lab 4：进程管理 |
| 小组成员 | 2413636-杨思远、2413681-王优、2413682-严苑毓 |
| 完成日期 | 2026-11-12（集成测试日期） |

### 小组分工

| 成员 | 负责的练习/模块 |
|---|---|
| 2413636-杨思远 | 功能模块一：虚拟内存页操作、物理内存管理前置代码；Challenge 2 |
| 2413681-王优 | 功能模块二：进程创建和初始化 |
| 2413682-严苑毓 | 功能模块三：调度和上下文切换；Challenge 1 |


## 一、实验目的

1. 理解 Sv39 多级页表、物理页引用计数和 TLB 维护的关系。
2. 掌握进程控制块、内核栈及内核线程初始执行现场的构造。
3. 理解调度、页表切换、上下文切换和中断临界区保护。
4. 借助 AI 辅助开发，并通过代码检查及实际测试验证结果。

## 二、实验环境

- 系统：Windows 主机，WSL Ubuntu 24.04；架构目标：RISC-V 64，Sv39。
- 工具：`riscv64-unknown-elf-gcc`、GNU Make、`qemu-system-riscv64`、`gdb-multiarch`、Python 3。
- 集成测试通过 `make grade GDB=gdb-multiarch` 指定调试器；GDB 路径属于环境配置。

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|---|---|---|---|
| 2413636-杨思远 | Codex | GPT6.1 | 提供了可复用提示词 |
| 2413681-王优 | Codex 桌面应用 | GPT6.1 | 提供了可复用提示词 |
| 2413682-严苑毓 | Codex | GPT6.1 | 提供了提示词汇总 |

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

实验沿“虚拟内存页操作 → 内核线程创建和初始化 → 线程调度及上下文切换”的主线，让内核从具备内存管理能力发展到能够创建并运行内核线程。主要执行路径为 `kern_init → pmm_init → vmm_init → proc_init → cpu_idle → schedule → proc_run → switch_to`。

### 2.2 功能的逐步实现

1. 先恢复物理页分配与内存初始化，实现页映射，以支持后续内核对象和地址空间管理。
2. 再创建进程控制块与内核栈，构造 `idleproc`、`initproc` 和线程的首次运行现场。
3. 最后选择可运行线程、切换地址空间及保存恢复 CPU 上下文。
4. 集成时补全实验三 Trap 依赖，调整启动映射和 Sv39 非叶子页表项，再运行内存、创建及调度测试。

## 四、实验内容与实现

### 功能模块一：虚拟内存页操作功能

**负责人：** 2413636-杨思远

#### 模块功能描述

涉及 `int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)` 和 `void page_remove(pde_t *pgdir, uintptr_t la)`，位于 `kern/mm/pmm.c`。插入时处理首次映射、同页更新和异页替换；删除时处理无映射与引用计数归零，并刷新 TLB。`get_pte()` 在本次新建中间表后遭遇分配失败时回滚本次分配。

#### 最终提示词

以下为 A 交付包提供的可复用提示词，并非推定其全部历史对话：

> 请以本项目实际源码为准，仅完成 Lab4 功能模块一。阅读 pmm.c / pmm.h / memlayout.h、get_pte、Page 引用计数接口、pte_create 和 TLB 失效接口，实现 page_insert、page_remove，允许增加内部删除辅助函数。覆盖空映射、同页重映射并更新权限、异页替换、引用归零释放、重复删除和页表页分配失败；保证失败不改变数据页引用计数，新增中间表分配失败时回滚本次新增资源。检查 Lab2 物理内存初始化和 best-fit 分配器空位，保留测试不变；不实现进程创建、调度或 switch_to，不引入按需分页。先复现缺失实现导致的失败，再用原有页表测试与独立 QEMU 测试验证，分别报告 A 测试及整项目构建结果。

#### 实现迭代过程

- A 交付记录恢复了 `page_init()` 与 Best-Fit 分配器，并对映射、计数、失败回滚和 TLB 进行测试。
- 集成环境复测时启动及低地址访问出现页故障：启动阶段切换分页后仍需执行物理地址指令；另外，Sv39 非叶子项不应带 U 位。集成版参考 C 包补充临时恒等映射、运行时三级页表及非叶子权限修正，并将原框架中要求非叶子 U 位置位的断言改为要求清零。A 的回滚逻辑保留。
- 集成后在 64、128、256 MiB 下，PMM、耗尽回滚和实际 TLB 替换测试通过。不能将此次测试解释为已实现超级页拆分或通用并发页表管理。

### 功能模块二：进程创建及初始化功能

**负责人：** 2413681-王优

#### 模块功能描述

在 `kern/process/proc.c` 中实现：

```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t stack, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

`alloc_proc` 初始化 PCB；`proc_init` 建立 idleproc 和 initproc；`kernel_thread` 准备线程入口；`do_fork` 组合 PCB、内核栈、PID、链表及现场并处理失败；`copy_thread` 建立首次运行的 trapframe 和 context；本阶段 `copy_mm` 不复制用户地址空间。

#### 最终提示词

<!--
你是一名熟悉 RISC-V 64、Sv39 和 uCore 的操作系统实验开发助手。

# 一、任务

请根据当前项目源码和实验手册，完成 Lab4“功能模块二：进程创建及初始化功能”。

目标是让内核能够初始化进程管理结构，建立 idleproc，并创建能够被调度执行的 initproc。

不要只给出示例代码。请先阅读项目，再在实际代码中完成修改，并尽可能运行测试。

# 二、任务边界

本次只负责以下六个函数：

1. static struct proc_struct *alloc_proc(void);
2. void proc_init(void);
3. int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
4. int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
5. static void copy_thread(struct proc_struct *proc, uintptr_t stack,
                           struct trapframe *tf);
6. static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);

主要修改 kern/process/proc.c。

不要覆盖或重新实现成员 A 的页操作代码，也不要覆盖成员 C 的调度和上下文切换代码。除非编译或接口兼容确实需要，否则不要修改其他文件；如必须修改，请说明原因和影响。

本实验不要求实现完整的用户进程、用户地址空间复制、进程退出及资源回收。不要声称占位的 do_exit() 已经实现完整退出流程。

# 三、实现前必须阅读的代码

请先阅读并总结与本任务直接相关的代码，至少包括：

- kern/process/proc.c：已有全局变量、辅助函数和待实现位置；
- kern/process/proc.h：proc_struct、context、进程状态和公开接口；
- kern/process/entry.S：kernel_thread_entry；
- kern/trap/trap.h：trapframe 和寄存器结构；
- kern/trap/trapentry.S：forkrets、__trapret；
- kern/schedule/sched.c 和 sched.h：wakeup_proc 等接口；
- kern/mm/pmm.h、kmalloc.h：页和内存分配接口；
- kern/sync/sync.h：中断保护接口；
- kern/init/init.c：proc_init 的调用位置；
- tools/test_lab4.c：进程创建及调度相关测试。

以当前项目中真实存在的函数、宏、结构体字段和代码约定为准，不要凭其他版本 uCore 的代码直接覆盖。

# 四、各函数要求

## 1. alloc_proc()

- 分配一个 proc_struct。
- 分配失败返回 NULL。
- 将进程状态、PID、内核栈、调度标志、父进程、mm、
  context、trapframe、页表地址、标志、名称及链表节点等
  初始化为适合当前框架的安全状态。
- 核对 proc.h 中的所有字段，不要遗漏。
- 内核线程使用的初始页表地址应与项目实际的内核页表约定一致。

## 2. proc_init()

- 初始化进程链表、PID 哈希链表等进程管理结构。
- 创建并初始化 idleproc，使其代表当前启动执行现场：
  PID 为 0、状态可运行，使用启动内核栈，并设置 current。
- 根据当前框架设置 idleproc 的调度标志及进程数量。
- 调用 kernel_thread() 创建 initproc，找到新线程并设置名称。
- 处理分配或创建失败，不要继续使用空指针。
- 核对 idleproc 是否应该加入 proc_list，确保与当前调度器的约定一致。

## 3. kernel_thread()

- 根据 RISC-V trapframe 的实际布局构造初始现场。
- 将线程函数和参数放在 kernel_thread_entry 实际读取的位置。
- 设置正确的首次执行入口和 sstatus。
- 调用 do_fork()，返回新线程 PID 或负错误码。
- 解释为什么需要设置 SPP、SPIE 等状态位；以项目约定为准。

## 4. do_fork()

- 检查进程数量限制。
- 按顺序完成 PCB 分配、内核栈分配、地址空间处理、
  线程现场构造、PID 分配、哈希/链表登记和线程唤醒。
- 设置父进程为 current。
- 使用已有辅助函数，不重复实现已有功能。
- 多个可能失败的步骤要有清晰的逆序清理路径，避免资源泄漏。
- 对共享进程管理结构的修改使用项目已有的中断保护机制。
- 成功返回新进程 PID；失败返回对应负错误码。

## 5. copy_thread()

- 在新进程自己的内核栈上放置 trapframe，并复制初始现场。
- 根据当前实验的 fork/线程约定设置返回值和栈指针。
- 设置 context，使新线程第一次被切换到时进入正确路径。
- 对照项目实际汇编，核对：
  switch_to → forkret → forkrets → __trapret → kernel_thread_entry。
- 不要将 context 和 trapframe 混为一谈。

## 6. copy_mm()

- 遵循手册中“当前阶段不实现完整用户地址空间复制”的要求。
- 保持函数接口完整，明确本阶段的限制。
- 不要伪称已经支持用户进程地址空间复制。

# 五、验证要求

1. 修改前检查当前工作区状态，保留已有内容。
2. 优先使用项目已有测试；能够添加测试时，先明确测试覆盖的行为。
3. 至少检查：
   - idleproc 的 PID、状态、内核栈、current 和关键字段；
   - initproc 是否创建成功、父进程和状态是否正确；
   - 新线程的 context 和 trapframe 是否已准备；
   - 分配失败时是否走对应清理路径；
   - 能否与现有调度代码和评分测试链接。
4. 如果 A、C 或实验 2/3 前置代码尚未完成，明确指出因此无法进行的测试；
   不要用临时桩函数冒充完整集成通过。
5. 能运行时执行项目适用的编译、QEMU 和评分命令，记录真实结果。
6. 最终报告列出：
   - 修改的文件和函数；
   - 关键实现及与 A、C 的接口；
   - 实际执行的命令和结果；
   - 未执行的测试及原因；
   - 已知限制和仍需联调的问题。

现在先阅读代码，再开始实现。

#### 实现迭代过程

- 按手册接口完成六个函数，保留调度部分供 C 实现；最初仅进行了 RISC-V 目标语法检查，当时缺少 GCC/QEMU。
- 安装工具并与 A、C 集成后，实际运行创建测试和调度测试；评分中的 Process Create 通过。
- 线程函数返回后的 `do_exit()` 仍是框架占位实现，会触发 panic；本实验没有实现完整进程退出和资源回收，不能声称覆盖完整进程生命周期。
-->

### 功能模块三：进程调度

**负责人：** 2413682-严苑毓

#### 模块功能描述

```c
void cpu_idle(void);
void schedule(void);
void proc_run(struct proc_struct *proc);
void switch_to(struct context *from, struct context *to);
```

`cpu_idle` 等待重调度；`schedule` 选择 runnable 线程或回退到 idleproc；`proc_run` 在中断保护下更新 current、切换 Sv39 根页表并刷新 TLB；`switch_to` 保存与恢复 `ra`、`sp`、`s0`—`s11`。

#### 最终提示词

C 交付包中的提示词汇总给出以下约束概要（为整理，不声称逐字复制全部原文）：在单核 RV64 Sv39 环境实现四个调度接口；`context` 按 `ra/sp/s0-s11` 布局；跳过非 runnable 线程、无候选时选 idle；保护切换临界区并恢复调用者中断状态；`satp` 使用 Sv39 格式和根表物理页号，不使用 Sv32 遗留接口。完整提示词可参考 C 原交付包 `report/prompt.md`。

#### 实现迭代过程

- 先在 C 的独立支撑代码中运行评分和增强测试；随后只合并 C 的正式调度代码，保留 A、B 的实现。
- 集成版本通过评分中的 Schedule 检查，并通过多线程反复切换、寄存器保存、不同根页表、睡眠线程跳过和 idle 回退等增强测试。

### Challenge 1：中断保存与恢复

**负责人：** 2413682-严苑毓

`local_intr_save(flag)` 读取 `sstatus.SIE`；原先开启则关闭中断并记录 true，原先关闭则记录 false。`local_intr_restore(flag)` 仅在记录为 true 时重新开启。这样嵌套临界区的内部恢复不会提前开启中断，外层最终恢复进入前状态。此机制保护本核关键区，不等同于多核互斥锁；本实验不实现多核同步。

### Challenge 2：分页模式与 get_pte

**负责人：** 2413636-杨思远

Sv32、Sv39、Sv48 在 4 KiB 基础页路径上分别使用 2、3、4 级页表。Sv32 每级 VPN 索引 10 位、PTE 4 字节；Sv39/Sv48 每级索引 9 位、PTE 8 字节。Sv39 从根表下降两次，两个层级都要检查有效位、必要时分配并清零下一级页表，所以 `get_pte()` 两段代码形似。

将查找与创建合在 `get_pte(..., create)` 中可以复用遍历逻辑，但 `create=1` 有分配副作用，NULL 的具体含义也依赖调用场景。当前实验可保留该接口；复杂系统可对外拆分只读查找和确保创建接口，内部共享遍历器，并明确失败回滚与并发规则。本实现面向基础页遍历，未实现高层超级页拆分。详见 `Challenge2.md`。

## 五、测试与验证

集成版本在 WSL Ubuntu 24.04、QEMU 下实际执行：

| 测试 | 结果 | 范围与说明 |
|---|---|---|
| `make -j2` | 通过，退出码 0 | 存在框架相关编译警告 |
| `make grade GDB=gdb-multiarch GRADE_TMP=/tmp/lab4-final-verification-739103` | 30/30，退出码 0 | PMM、Process Create、Schedule 均 OK；使用集成版 QEMU 启动配置和修正后的非叶子断言 |
| `bash tests/a/run.sh` | 通过，退出码 0 | 64/128/256 MiB，PMM、内存耗尽回滚、TLB 替换 |
| `python3 tests/c/run_qemu_checks.py` | 通过，退出码 0 | 在临时副本注入测试；寄存器、调度、页表及中断状态 |
| `timeout 8s make qemu` | 输出 initproc 信息后进入占位 panic，命令退出码 0 | 验证线程首次运行；不是正常关机测试 |

**实际限制：**`init_main` 返回后调用的 `do_exit()` 是原框架占位 panic，因此普通启动会进入内核监视器；这不是无错误正常退出。`make grade` 能通过不代表退出回收已实现。测试日志见同目录 `测试记录/`。



## 六、实验总结与收获

### 对操作系统的理解

1. 页表将虚拟页映射到物理页；引用计数管理共享物理页寿命，TLB 缓存翻译结果，三者必须协调。
2. PCB 保存调度和资源元数据，`context` 保存函数调用约定要求跨切换保留的寄存器；`trapframe` 则用于异常返回，二者作用不同。
3. 调度策略负责选择线程，上下文切换负责保存恢复执行状态；关中断只保护本核临界区，不天然解决多核并发。
4. 教学实验仅实现简化内核线程机制；用户态地址空间复制、完整退出回收、抢占式时间片、多核与文件系统等重要 OS 原理未在本实验中完整实现。

### AI 协作开发的经验

应让 AI 以实际接口和代码为依据，明确任务边界及失败场景；生成后检查资源管理和汇编布局。独立测试与整合测试不能互相替代：本次 A 和 C 分别有独立验证，但整合时仍需处理启动页表、页表权限及调试器配置。报告只记录可追溯的提示词、测试结果与未完成部分，不把未知资料编造成事实。
