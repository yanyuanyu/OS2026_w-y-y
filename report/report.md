# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|---|---|
| **实验名称** | Lab 1：比麻雀更小的麻雀（最小可执行内核） |
| **小组成员** | 严苑毓-2413682；杨思远-2413636；王优-2413681 |
| **完成日期** | 2026-10-07（整合更新） |

### 小组分工

| 成员 | 负责的练习/模块 |
|---|---|
| 严苑毓 | 阅读 `entry.S`、`init.c` 和链接脚本；分析内核入口、启动栈、`kern_init()` 与 `0x80200000`；完成练习1 |
| 杨思远-2413636 | 阅读 `Makefile`、`function.mk` 和 SBI 输出相关代码；分析编译、链接、镜像生成和字符输出链路 |
| 王优-2413681 | 配置 QEMU 与 GDB，跟踪复位地址、OpenSBI 入口和内核入口；完成练习2的调试记录 |

A、C 提供了编译、运行和 GDB 记录；B 提供了独立的清理构建、镜像检查与 QEMU 运行日志。B 的交付材料未包含 GDB 会话截图；答辩前三位成员均应亲自走读 GDB 启动链并能解释关键寄存器。

> **报告范围说明：** 本文合并了 A 的练习 1 分析与截图、B 的构建和 SBI/`cprintf` 交付记录，以及 C 的 GDB 实测。各成员环境与命令按实际材料分别记录；成员姓名和学号已按组长提供的信息补齐。B 的 GDB 独立复测未在其交付材料中记录；C 的 GDB 实测见报告正文。

---

## 一、实验目的

本实验的主要目的是：

1. 掌握 RISC-V 交叉编译的基本流程，理解 ELF 内核 `bin/kernel` 与裸二进制镜像 `bin/ucore.img` 的区别。
2. 理解 QEMU、OpenSBI 和 uCore 内核在启动过程中的职责边界，验证 `0x1000 → 0x80000000 → 0x80200000` 的控制流转移。
3. 理解链接脚本如何规定内核入口和内存布局，掌握 `kern_entry` 设置启动栈并进入 `kern_init()` 的过程。
4. 熟悉 QEMU GDB Server 和 RISC-V GDB 的基本用法，能够通过断点、单步执行和寄存器检查验证启动流程。
5. 结合源码、反汇编和实际运行结果理解 BSS 清零、函数栈帧、SBI 字符输出及内核不返回的原因。

---

## 二、实验环境

### 2.1 软件环境（按成员分别记录）

本组在不同成员的 WSL 电脑上完成实验，工具版本存在差异。以下版本分别对应各自的记录和截图，不应互相替代。

| 成员/记录来源 | Linux 环境 | GCC | GDB | QEMU | Make |
|---|---|---|---|---|---|
| A：严苑毓-2413682（A 的报告与截图） | WSL2 Ubuntu 22.04 | `riscv64-unknown-elf-gcc 15.1.0` | GNU GDB `16.3.90.20250610-git` | `qemu-system-riscv64 7.0.0` | GNU Make 4.3 |
| C：王优-2413681（当前工作区实测） | WSL2 Ubuntu 22.04.5 | `riscv64-unknown-elf-gcc 10.2.0` | 多架构 GDB 12.1，通过 `riscv64-unknown-elf-gdb` 调用 | `qemu-system-riscv64 6.2.0` | GNU Make 4.3 |
| B：杨思远-2413636（交付记录） | WSL2 Ubuntu 22.04.5 | `riscv64-unknown-elf-gcc 10.2.0` | 未在交付记录中注明 | QEMU 6.2.0 | 未在交付记录中注明 |

A 环境中的 `cprintf` 符号记录为 `0x80200054`；C 当前工作区的 GCC 10.2 构建中该符号为 `0x80200056`。这是不同编译环境生成的符号布局差异，报告中涉及具体地址时以对应成员的实测为准。两边共同验证的内核入口、`kern_init` 和 `bootstacktop` 地址分别为 `0x80200000`、`0x8020000a` 和 `0x80203000`。
### 2.2 AI 工具

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|---|---|---|---|
| 严苑毓-2413682 | Codex 桌面应用 | GPT-5.6-sol（按 A 提供记录，提交前核对客户端版本） | 用于阅读本地材料、解释启动原理、生成调试计划、分析命令输出和整理报告 |
| 杨思远-2413636 | Codex Desktop（B 交付报告记载） | GPT-6（交付报告记载；具体版本未注明） | 用于构建链路/SBI 分析与记录整理；本次未修改内核源代码 |
| 王优-2413681 | Codex（按本次报告整理记录） | 未在交付材料中注明 | 报告整理/调试说明辅助；实验操作以本人记录为准 |

### 2.3 QEMU 版本兼容说明

课程代码的 `Makefile` 使用 `-device loader,file=...,addr=0x80200000`。A（QEMU 7.0.0）、B 与 C（QEMU 6.2.0）的记录都显示：该启动方式下 OpenSBI 的 `Domain0 Next Address` 为 `0x0`，没有进入内核。A/C 通过 `-kernel bin/ucore.img`、B 通过 `-kernel bin/kernel` 让 QEMU 识别内核入口；对应记录中 OpenSBI 的下一阶段地址为 `0x80200000`，随后出现内核启动字符串。两种显式加载方式分别来自各成员实际记录，报告保留原始差异。QEMU 先把镜像/ELF 装载到客体内存，再把下一阶段地址交给 OpenSBI；OpenSBI 负责固件初始化和控制权移交，并不从磁盘读取本实验镜像。该兼容处理未修改内核源代码。

---

## 三、实验整体逻辑分析

### 3.1 本章实验的逻辑主线

本实验围绕“一个最小 RISC-V 内核怎样从源代码变成可运行镜像，并从 CPU 复位状态进入 C 语言内核入口”展开。整体流程如下：

```text
C/汇编源代码
    ↓ RISC-V交叉编译与链接
bin/kernel（ELF，包含符号和调试信息）
    ↓ objcopy --strip-all -O binary
bin/ucore.img（裸内核镜像）
    ↓ QEMU -kernel 加载 ELF 或裸镜像并启动虚拟RISC-V计算机
0x1000（QEMU virt复位ROM）
    ↓ 读取OpenSBI入口并执行jr t0
0x80000000（OpenSBI固件入口）
    ↓ 固件初始化并以S-mode跳转
0x80200000（uCore的kern_entry）
    ↓ 设置sp为bootstacktop
0x8020000a（kern_init）
    ↓ BSS清零、cprintf输出、进入死循环
```

### 3.2 功能的逐步实现与验证

1. **编译和链接内核**：Makefile 调用 RISC-V 工具链编译 `entry.S`、`init.c`、控制台、格式化输出和 SBI 相关代码，再使用链接脚本生成 `bin/kernel`。
2. **生成裸镜像**：`objcopy` 去除 ELF 容器信息，生成可供 QEMU `-kernel` 加载的 `bin/ucore.img`；B 的记录也验证了通过 `-kernel bin/kernel` 加载 ELF 的启动路径。
3. **确认静态入口和内存布局**：`readelf` 验证 ELF 入口为 `0x80200000`；`nm` 验证 `kern_entry`、`kern_init`、`bootstacktop`、`edata` 和 `end` 的地址。
4. **正常启动验证**：QEMU 启动 OpenSBI，OpenSBI 显示下一阶段地址为 `0x80200000`，随后内核输出 `(THU.CST) os is loading ...`。
5. **动态调试验证**：使用 `-s -S` 暂停虚拟 CPU 并开放 GDB 端口，依次在 `0x80000000`、`0x80200000`、`kern_init` 和 `cprintf` 设置断点，验证实际控制流和寄存器状态。
6. **深入分析练习1**：结合 `entry.S`、`kernel.ld`、反汇编和 GDB 结果，解释启动栈设置、尾调用和 C 入口初始化。

---

## 四、实验内容与实现

### 功能模块：练习1——理解内核启动中的程序入口操作

**负责人：** 严苑毓-2413682

#### 模块功能描述

**涉及的入口和函数：**

```asm
kern_entry:
    la sp, bootstacktop
    tail kern_init
```

```c
int kern_init(void) __attribute__((noreturn));
```

本模块负责把 OpenSBI 交付的最早期执行环境转换成 uCore 可以运行 C 代码的环境。OpenSBI 跳转到 `0x80200000` 后，`kern_entry` 首先把栈指针切换到 uCore 自己预留的启动栈，然后直接进入 `kern_init()`。`kern_init()` 清理未初始化数据区、输出启动信息并停留在内核主控制流中。

#### 最终提示词

以下为本次实际提问经过归并、补全后的核心有效提示词。它借鉴了往届报告“围绕练习问题组织证据”的形式，但分析对象、地址和结论均以当前源码及本次实测为准：

```markdown
[角色] 你是熟悉 RISC-V 启动过程的操作系统实验助教。
[材料] 读取当前 Lab 1 的 tools/kernel.ld、kern/init/entry.S、kern/init/init.c、Makefile、当前编译产物以及实际 GDB 输出。
[任务] 回答练习 1：说明 0x80200000 的来源；逐条解释 la sp, bootstacktop 与 tail kern_init 的伪指令展开、寄存器变化和控制流效果；分析启动栈的地址范围；解释 kern_init 中 BSS 清零、cprintf 与自跳转死循环。
[证据] 每个重要结论标明“源码事实”“工具实测”或“原理解释”，并用 readelf、nm、反汇编及 GDB 中的 sp、bootstacktop、edata、end 地址交叉验证。
[边界] 不生成或替换实验源码，不引用其他版本 uCore 的中断或内存管理内容，不虚构未执行的测试。
[输出] 先给符号地址证据表，再按指令顺序讲解，最后形成可以直接放入报告的练习 1 答案和答辩要点。
```

环境核验、QEMU 兼容性诊断、GDB 调试设计、截图审查、报告生成和提交验收等完整提示词见同目录的 [`prompt.md`](./prompt.md)。

#### 实现迭代过程

本实验没有要求补写函数或修改内核代码，迭代对象是实验理解、调试方法与证据链。

##### 第一次迭代：静态阅读代码

**完成内容：**

- 阅读链接脚本，确认 `ENTRY(kern_entry)` 和 `BASE_ADDRESS = 0x80200000`。
- 阅读 `entry.S`，确认启动栈由 `.space KSTACKSIZE` 预留，`bootstacktop` 位于栈空间高地址端。
- 阅读 `init.c`，确认 `kern_init()` 依次执行 BSS 清零、启动信息输出和无限循环。

**发现的问题：** 只阅读源码不能证明 CPU 实际经过哪些地址，也不能确认伪指令的机器码展开结果。

##### 第二次迭代：编译产物与符号验证

**完成内容：**

- 使用 `readelf` 确认 ELF 类型为 RISC-V 64位可执行文件，入口地址为 `0x80200000`。
- 使用 `nm` 得到 `kern_entry=0x80200000`、`kern_init=0x8020000a`、`bootstacktop=0x80203000`、`edata=end=0x80203008`。
- 对比 `bin/kernel` 与 `bin/ucore.img`，理解前者供 GDB 加载符号，后者供 QEMU 运行。

##### 第三次迭代：QEMU 7启动兼容修正

**遇到的问题：** 原 `make qemu` 参数在当前 QEMU 7.0.0 中使 OpenSBI 显示 `Next Address=0x0`，无法进入内核。

**解决策略：** 保留源码不变，使用 `-kernel bin/ucore.img` 启动。修正后 OpenSBI 显示 `Next Address=0x80200000`，内核成功输出启动信息。

##### 第四次迭代：GDB动态验证

**最终结果：**

- 验证复位 PC 为 `0x1000`；
- 验证复位ROM跳转到 OpenSBI 的 `0x80000000`；
- 验证 OpenSBI 跳转到 `0x80200000 <kern_entry>`；
- 单步执行后验证 `sp=bootstacktop=0x80203000`；
- 验证 `kern_init=0x8020000a`，并命中 `cprintf=0x80200054`；
- GDB 脱离后 QEMU 正常输出启动信息。

#### 练习1答案

##### 1. 内核入口地址为什么是 `0x80200000`

链接脚本中：

```ld
ENTRY(kern_entry)
BASE_ADDRESS = 0x80200000;
. = BASE_ADDRESS;
```

`ENTRY(kern_entry)` 指定 ELF 入口符号，`BASE_ADDRESS` 和位置计数器规定 `.text` 从 `0x80200000` 开始布局。编译后 `readelf` 显示入口地址为 `0x80200000`，`nm` 显示 `kern_entry` 也位于 `0x80200000`，静态规则与实际产物一致。

![内核入口地址和关键符号](./images/02_symbols.png)

##### 2. `la sp, bootstacktop` 完成的操作及目的

`entry.S` 先在 `.data` 段中定义启动栈：

```asm
.align PGSHIFT
bootstack:
    .space KSTACKSIZE
bootstacktop:
```

`.space KSTACKSIZE` 在链接时预留实际栈空间；`la` 本身不分配栈，只把已经存在的 `bootstacktop` 地址装入栈指针 `sp`。实测符号地址为：

```text
bootstack    = 0x80201000
bootstacktop = 0x80203000
```

二者相差 `0x2000`，即 8192 字节。RISC-V 栈向低地址增长，因此初始 `sp` 应指向高地址端 `bootstacktop`。

反汇编中该伪指令展开为：

```asm
0x80200000: auipc sp,0x3
0x80200004: mv    sp,sp
```

第一条以当前 PC 为基准加 `0x3000`，得到 `0x80203000`；由于目标地址低12位偏移为0，第二条等价于 `addi sp,sp,0`，被反汇编器显示为 `mv sp,sp`。单步执行两条机器指令后：

```text
sp            = 0x80203000
&bootstacktop = 0x80203000
```

二者相等，证明内核已从 OpenSBI 的临时栈切换到自己的启动栈。设置栈是进入 C 代码的必要条件，因为函数调用、局部变量、保存返回地址和寄存器都依赖有效栈空间。

![uCore内核入口](./images/06_kernel_0x80200000.png)

![内核启动栈初始化结果](./images/07_stack.png)

图中 GDB 在 `0x80203000` 后显示 `<SBI_CONSOLE_PUTCHAR>`，是因为另一个全局数据符号恰好与 `bootstacktop` 位于同一地址边界；`p/x (long)&bootstacktop` 已直接证明该地址确实是启动栈顶，并不表示栈指向输出函数。

##### 3. `tail kern_init` 完成的操作及目的

`tail` 是汇编器提供的尾调用伪指令。在当前距离范围内，它被展开为：

```asm
0x80200008: j 0x8020000a <kern_init>
```

执行前 PC 为 `0x80200008`，执行后 PC 变为 `0x8020000a`，控制权直接转交给 `kern_init()`。与普通 `call` 不同，`tail` 不把返回地址写入 `ra`，因为 `kern_entry` 后面没有需要继续执行的代码，`kern_init` 也被声明为 `noreturn`。这样既符合启动过程的单向控制流，也避免建立无意义的返回链路。

##### 4. `kern_init()` 的初始化行为

`kern_init()` 首先执行：

```c
memset(edata, 0, end - edata);
```

用于把需要零初始化的区域清零。当前最小内核实测：

```text
edata = 0x80203008
end   = 0x80203008
```

因此本次构建中清零长度为0，说明当前没有实际占用空间的未初始化全局数据。保留该代码是为了后续实验加入 BSS 数据后仍能正确初始化。

随后：

```c
cprintf("%s\n\n", message);
```

通过 `cprintf → vcprintf/vprintfmt → cons_putc → sbi_console_putchar → ecall → OpenSBI → QEMU串口` 输出启动信息。

最后：

```c
while (1)
    ;
```

被编译为：

```asm
0x8020003a: j 0x8020003a
```

该指令的当前地址与跳转目标相同，每次执行后 PC 仍为 `0x8020003a`，因此形成无限循环，防止内核入口函数返回到无效地址。

![进入kern_init并检查BSS边界](./images/08_kern_init.png)

### 功能模块：构建流程、SBI 输出与 `cprintf`

**负责人：** 杨思远-2413636

#### 模块目标与代码范围

本模块阅读 `Makefile`、`tools/function.mk`、`tools/kernel.ld` 和 SBI/console/stdio 代码，说明源文件如何生成 RISC-V 内核及镜像，并追踪 `cprintf` 字符如何经 SBI 到达 QEMU 串口。B 提交的源码与本组当前代码逐文件校验一致；本次是阅读、构建和运行验证，没有修改内核源代码。

涉及函数包括：

```c
int kern_init(void);
int cprintf(const char *fmt, ...);
int vcprintf(const char *fmt, va_list ap);
void cons_putc(int c);
void sbi_console_putchar(unsigned char ch);
uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0,
                  uint64_t arg1, uint64_t arg2);
```

#### Makefile、依赖规则与构建产物

`Makefile` 选择 `riscv64-unknown-elf-` 交叉工具链，并收集 `libs/` 与内核目录中的 `.c`、`.S` 文件。宿主机虽然是 x86-64 Ubuntu/WSL，编译目标仍是 RV64；`-nostdinc`、`-nostdlib` 避免错误依赖宿主机 C 运行库，`-mcmodel=medany` 适合链接到较高地址的内核代码，`-g` 保留 GDB 所需调试信息。`-ffunction-sections`、`-fdata-sections` 与链接选项 `--gc-sections` 配合，可按段丢弃未引用内容。

`tools/function.mk` 把源文件映射到 `obj/` 下的 `.o` 与 `.d`：依赖生成规则通过编译器 `-MM` 记录头文件依赖，编译规则通过 `-c` 生成对象文件；后续头文件变化会触发相应对象重编译。对象文件汇总后交给链接器。`tools/kernel.ld` 使用 `OUTPUT_ARCH(riscv)`、`ENTRY(kern_entry)` 和 `BASE_ADDRESS = 0x80200000` 布置 `.text`、只读数据、可写数据和 BSS 等段。

```text
.c/.S → obj/**/*.o（及头文件依赖 .d）
      → ld + tools/kernel.ld → bin/kernel（ELF64 RISC-V，可供符号检查/GDB）
      → objcopy --strip-all -O binary → bin/ucore.img（裸二进制镜像）
```

`bin/kernel` 是带 ELF 头、段表和调试符号的可执行文件；`bin/ucore.img` 是去除 ELF 容器信息后的裸字节镜像。B 的构建日志明确显示 `ld bin/kernel` 和 `riscv64-unknown-elf-objcopy ... bin/ucore.img` 均执行成功，`readelf` 检查得到 ELF64、Machine 为 RISC-V，入口地址为 `0x80200000`。

#### SBI 与 `cprintf` 字符输出链路

```text
kern_init()
  → cprintf()
  → vcprintf()
  → vprintfmt()
  → cputch()
  → cons_putc()
  → sbi_console_putchar()
  → sbi_call()
  → ecall
  → OpenSBI 提供的控制台服务
  → QEMU 虚拟串口 / -nographic 终端
```

`cprintf` 管理可变参数并调用 `vcprintf`；`vcprintf` 将格式串交给 `vprintfmt`，再由 `cputch` 逐字符输出。`cons_putc` 转发到 `sbi_console_putchar`。本代码使用 legacy SBI console putchar 调用号 1：`sbi_call` 把调用号放入 `a7/x17`、字符参数放入 `a0/x10`，执行 `ecall`。运行在 S-mode 的内核因此请求 M-mode 固件提供控制台服务，而不是直接调用宿主机的 `printf`。QEMU 的 `-nographic` 把虚拟串口连接到当前终端，所以可以看到 `(THU.CST) os is loading ...`。

#### 构建与运行记录

B 的交付环境为 WSL2 Ubuntu 22.04.5、`riscv64-unknown-elf-gcc 10.2.0`、QEMU 6.2.0、OpenSBI v0.9（运行时 SBI 0.2）。执行 `make clean && make` 后返回码为 0，生成 48,712 字节的 `bin/kernel` 和 12,296 字节的 `bin/ucore.img`。构建过程中出现 WSL 文件时间轻微超前的 `Clock skew detected` 警告，但没有编译或链接失败；ELF 入口为 `0x80200000`。

B 也验证了启动参数差异：压缩包的 `make qemu` 使用 `-device loader`，本机记录的 OpenSBI `Next Address` 为 `0x0`，观察不到内核输出；改用 `qemu-system-riscv64 -machine virt -nographic -bios default -kernel bin/kernel` 后，OpenSBI 报告 `Next Address = 0x80200000`，并输出内核字符串。内核随后按设计进入 `while (1)` 空转，因此 5 秒观察命令由 `timeout` 终止、返回码 124；这是观察时限结束，不是构建失败或内核崩溃。

B 的逐条命令和终端原始输出见 [B 部分运行记录](./B部分-完整运行记录.log)。该交付包未包含 B 的独立截图；本报告的编译、QEMU 和 GDB 截图分别来自团队已提交的 A/C 实测记录，不将其标为 B 的个人截图。

#### 提示词与实现迭代

B 报告说明本模块未修改内核代码，因此不存在代码生成与代码修复迭代。B 的原始 AI 对话提示词没有随压缩包提供；`prompt.md` 中另列了依据 B 交付内容整理的任务摘要，并明确标注为归纳稿而非原始逐字记录。B 交付报告记载使用 Codex Desktop/GPT-6 辅助分析和整理，具体子版本未注明。

---
### 共同实操：练习2——使用GDB验证启动流程

以下启动链由 A 的 GDB 记录和 C 的本机复测共同验证。B 提供了构建与 QEMU 启动日志，但未提供个人 GDB 会话；全组答辩前应能共同讲解这些断点和寄存器结果。

#### 1. QEMU调试服务器

QEMU 参数 `-s` 等价于 `-gdb tcp::1234`，在本机1234端口开放 GDB Server；`-S` 在启动时冻结虚拟 CPU。GDB 使用：

```gdb
target remote localhost:1234
```

连接后读取当前寄存器，观察到复位 PC 为 `0x1000`、`sp=0`。

#### 2. 复位ROM `0x1000`

```asm
0x1000: auipc t0,0x0
0x1004: addi  a2,t0,40
0x1008: csrr  a0,mhartid
0x100c: ld    a1,32(t0)
0x1010: ld    t0,24(t0)
0x1014: jr    t0
```

该代码由 QEMU `virt` 机器提供，不属于 uCore 源码。它读取 HART ID 和启动参数，从复位ROM数据区取得 OpenSBI 入口地址，并通过 `jr t0` 跳转到 `0x80000000`。

![CPU复位入口0x1000](./images/04_reset_0x1000.png)

#### 3. OpenSBI入口 `0x80000000`

在 `0x80000000` 设置断点后，CPU成功停在 OpenSBI 首条指令，`pc=0x80000000`、`sp=0`。这说明系统仍处于固件最早期阶段，后续由 OpenSBI 建立运行环境并把内核作为下一阶段程序启动。

![OpenSBI入口0x80000000](./images/05_opensbi_0x80000000.png)

#### 4. 内核入口 `0x80200000`

继续执行后命中 `0x80200000 <kern_entry>`，证明 OpenSBI 的下一阶段地址、链接脚本入口和实际控制流三者一致。随后单步验证启动栈设置，并继续命中 `kern_init` 与 `cprintf`。

![GDB命中cprintf](./images/09_cprintf.png)

GDB执行 `detach` 后，虚拟CPU继续运行，QEMU终端成功输出启动字符串。

![GDB脱离后内核继续输出](./images/10_output.png)

---

### 成员 C 的本机复测与证据

**负责人：** 王优-2413681

C 在本机 WSL2 Ubuntu 22.04.5、GCC 10.2.0、QEMU 6.2.0 和 GDB 12.1 环境中重复验证。调试时使用 `-kernel bin/ucore.img -S -s` 启动 QEMU，并在另一个终端加载 `bin/kernel` 的符号。

本机实测顺序与 A 的结果一致：GDB 初始 `pc=0x1000`；执行六条复位代码后到达 `0x80000000`；设置 `break *0x80200000` 后停在 `kern_entry`；入口单步后 `sp=0x80203000`；随后命中 `kern_init`。QEMU 串口显示 `(THU.CST) os is loading ...`。内核进入 `while (1)` 后持续运行是预期现象。

C 环境的 `cprintf` 符号为 `0x80200056`，与 A 环境记录的 `0x80200054` 不同；这是编译环境差异，报告未将其合并为一个地址。当前启动参数使用 `-kernel`，镜像在 CPU 开始执行前由 QEMU 放入内存，因此 GDB 的 `watch *0x80200000` 不会捕捉这次预加载；C 以入口断点验证控制流交接。

**图 C-1：C 本机从复位地址跟踪至 OpenSBI、内核入口，并单步观察 SP。**

![C 的 GDB 启动跟踪记录](./images/C_GDB_启动跟踪.png)

**图 C-2：C 本机命中 `kern_init` 断点。**

![C 的 GDB kern_init 断点记录](./images/C_GDB_kern_init断点.png)

**图 C-3：C 本机 QEMU/OpenSBI 信息和内核启动输出。**该终端滚屏包含前一次 QEMU 退出记录，内核启动信息来自实际运行。

![C 的 QEMU 内核输出记录](./images/QEMU_内核输出.png)

---
## 五、测试与验证

### 5.1 编译与镜像生成

A、B、C 的记录均显示构建成功。B 的完整命令输出见 [B 部分运行记录](./B部分-完整运行记录.log)；A 的构建截图如下。成员日志中的文件大小和符号布局有差异，报告按各自日志分别记录，不将差异简单归因于单一工具版本。

执行：

```bash
make clean
make
ls -lh bin
```

实际生成：

```text
bin/kernel     44K
bin/ucore.img  13K
```

![Lab1内核编译及镜像生成](./images/01_build.png)

### 5.2 正常启动验证

A 的 QEMU 7.0.0 运行截图和 C 的 QEMU 6.2.0 复测均使用裸镜像启动：

```bash
qemu-system-riscv64 \
  -machine virt \
  -nographic \
  -bios default \
  -kernel bin/ucore.img
```

OpenSBI显示：

```text
Firmware Base        : 0x80000000
Domain0 Next Address : 0x80200000
Domain0 Next Mode    : S-mode
```

内核最终输出：

```text
(THU.CST) os is loading ...
```

B 的 QEMU 6.2.0 日志则使用 `-kernel bin/kernel` 加载 ELF，同样观察到 `Next Address=0x80200000` 和内核输出，命令与原始输出见 B 部分运行记录。

![OpenSBI将控制权交给内核](./images/03_boot.png)

### 5.3 关键检查点汇总

| 检查项 | 预期结果 | 实测结果 | 状态 |
|---|---|---|---|
| 编译和链接 | 生成 `bin/kernel` | A：约44K；B：48,712字节；B日志返回码0 | 通过 |
| 镜像生成 | 生成 `bin/ucore.img` | A：约13K；B：12,296字节；B日志返回码0 | 通过 |
| ELF架构 | RISC-V 64位 | `Machine: RISC-V` | 通过 |
| ELF入口 | `0x80200000` | A/B/C 的构建/检查记录均为 `0x80200000` | 通过 |
| CPU复位入口 | `0x1000` | `pc=0x1000` | 通过 |
| OpenSBI入口 | `0x80000000` | `pc=0x80000000` | 通过 |
| uCore入口 | `0x80200000` | `pc=0x80200000 <kern_entry>` | 通过 |
| 内核启动栈 | `sp=bootstacktop` | 均为 `0x80203000` | 通过 |
| C语言入口 | 进入 `kern_init` | `pc=0x8020000a` | 通过 |
| BSS边界 | 可由链接符号确定 | `edata=end=0x80203008` | 通过 |
| 格式化输出 | 命中 `cprintf` / 确认 SBI 输出 | A：`cprintf=0x80200054`；C：`0x80200056`；B：QEMU 串口出现启动字符串 | 通过 |
| `make qemu` 原始目标 | OpenSBI 下一跳为内核入口 | B 日志 `Next Address=0x0`，未出现内核输出 | 需兼容参数 |
| 显式 `-kernel` 启动 | OpenSBI 下一跳 `0x80200000` 并出现内核输出 | A/C 加载裸镜像；B 加载 ELF；日志/截图均见报告 | 通过 |
| 启动信息 | 输出指定字符串 | `(THU.CST) os is loading ...` | 通过 |

当前代码包未提供 `tools/grade.sh`，因此通用报告模板中的 `make grade` 在本实验目录不可用。本实验以编译成功、ELF/符号检查、QEMU启动输出和GDB关键地址链作为验收证据。

---

## 六、实验总结与收获

### 6.1 对操作系统的理解

| 实验知识点 | 对应OS原理 | 含义、关系与差异 |
|---|---|---|
| QEMU复位入口 `0x1000` | CPU复位与启动向量 | 复位PC由硬件平台规定；本实验中该“硬件”由QEMU实现，因此地址不在uCore源码中 |
| OpenSBI `0x80000000` | 固件与特权级 | OpenSBI运行在更高特权级，完成早期初始化并为S-mode内核提供SBI服务 |
| `ENTRY(kern_entry)` 与 `0x80200000` | 程序装载与地址空间布局 | 链接地址、加载地址和跳转地址必须一致，否则CPU不能按预期解释代码中的地址 |
| `la sp,bootstacktop` | 函数调用栈 | 在进入C函数前建立内核自己的栈，使局部变量、返回地址和寄存器保存具有可靠空间 |
| `tail kern_init` | 控制权移交与尾调用 | 启动过程是单向的，内核入口不需要返回，因此不建立无意义的返回链路 |
| BSS清零 | C语言运行时约定 | 未显式初始化的全局变量必须从0开始；裸机内核需要自己完成这一步 |
| SBI字符输出 | 内核与固件接口 | uCore不调用宿主Windows/Linux的输出函数，而是通过 `ecall` 请求OpenSBI和虚拟串口输出 |
| GDB远程调试 | 内核可观测性 | 软件调试器可以暂停虚拟CPU、读取寄存器和反汇编，验证仅靠最终输出无法证明的启动细节 |

### 6.2 OS原理中重要但本实验未覆盖的知识点

1. **中断与异常处理**：本实验尚未建立完整的中断向量、上下文保存与恢复流程。
2. **虚拟内存与页表**：当前地址主要作为启动阶段的固定物理/链接地址使用，尚未配置面向进程的虚拟地址空间。
3. **进程与调度**：内核启动后停留在死循环，尚未创建任务或进行上下文切换。
4. **动态内存管理**：尚未实现页分配器、堆或内核对象分配。
5. **文件系统与设备驱动**：除最小控制台输出外，尚未涉及块设备、文件系统和通用驱动框架。

### 6.3 AI协作开发的经验

本实验中AI的主要作用不是生成代码，而是帮助建立“源码—编译产物—运行现象—调试证据”的对应关系。实践中得到以下经验：

1. 对AI给出的启动结论必须使用当前环境验证。例如原Makefile面向较老QEMU，当前QEMU 7.0.0的行为不同，只有检查OpenSBI的 `Next Address` 才能发现兼容问题。
2. 提问时需要提供具体文件、命令输出和地址，避免只问“为什么运行不了”。当输入包含Makefile、QEMU版本和OpenSBI输出时，才能把环境兼容问题与代码错误区分开。
3. AI可以解释伪指令和反汇编，但最终应由GDB的寄存器结果验证。例如 `sp=bootstacktop=0x80203000` 比单纯复述 `la` 的定义更有说服力。
4. 报告中的每个结论都应对应实际截图或源码证据，不引用其他年份Lab1中断实验、不同地址布局或未执行的测试结果。

通过本实验，我们完成了从源代码到内核镜像、从CPU复位到C语言入口及 SBI 字符输出的观察，建立了后续操作系统实验所需的编译、运行和调试基础。B 是否还需补充个人 GDB 复测，组长可按课程要求核对。


