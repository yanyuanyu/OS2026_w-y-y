# Lab2 提示词汇总

小组成员：2413681 王优、2413682 严苑毓、2413636 杨思远。

本文件汇总本实验的六份复现任务规格：物理内存初始化、First-Fit 分析与 Best-Fit 实现、Buddy 实现与接入、Buddy 独立测试、SLUB 实现、SLUB 独立测试。这些规格依据成员交付材料与最终采用代码整理，用于复现和审查；其中 SLUB 两份规格对应王优交接的实现与测试说明。AI 工具及模型按组内确认填写。全部规格对照老师《提示词如何撰写》复核。各模块对应的实现、验证与迭代过程见 [实验报告](report.md)。

# 模块一：物理内存初始化

负责人：2413681 王优。按最终源码整理，并对照《提示词如何撰写》复核的任务规格。

[PROMPT]

任务：在 kern/mm/pmm.c 的 lab2 function1 标记处完成物理内存初始化，使内核能够依据 DTB 建立 Page 描述符，保留已占用区域，并把剩余完整页交给当前物理页分配器。
操作要求：直接修改工程中的实际文件；实现范围为 page_init() 及其内部辅助逻辑。沿用现有 DTB 解析、启动映射、公共头文件和 PMM 接口，保留原检查函数、lab2 标记及负责人学号。
输出要求：交付可构建的源码，输出管理的物理范围、描述符数量及地址、空闲起点；执行构建和初始化边界检查，记录命令、结果及异常原因。

[RELY]

- get_memory_base()/get_memory_size() 返回字节单位的 DRAM 基址和长度。
- PGSIZE、PPN、ROUNDUP/ROUNDDOWN、PHYSICAL_MEMORY_OFFSET、PADDR、pa2page、链接符号 end，以及 nbase=DRAM_BASE/PGSIZE。
- struct Page 的 flags、property、ref，SetPageReserved、set_page_ref，以及已初始化的 pmm_manager->init_memmap(base,n)。
- 当前 QEMU virt 使用从 DRAM_BASE 起的固定基址单段 DRAM，启动页表覆盖其中的 1 GiB 窗口。

[GUARANTEE]

```c
static void page_init(void);
```

该函数建立 va_pa_offset、npage、pages 和当前 PMM 的初始空闲状态，供 pmm_init() 后续检查和分配使用。

[SPECIFICATION]

## page_init

Pre-Condition：DTB 解析完成，内核运行于规定直映区，当前 PMM 已执行 init。

Post-Condition（成功）：va_pa_offset 等于工程规定的直映偏移；npage 是管理范围末端的排他物理页号；pages[0] 对应 nbase，数组共有 npage-nbase 项。描述符数组从 ROUNDUP(end,PGSIZE) 开始。固件窗口、内核和描述符数组占用的页面保持 Reserved；数组之后到管理末端的完整页已登记到当前 PMM，初始空闲页数与该区间页数一致。

Case 1（有效范围）：物理末端按页向下对齐；尾部不足一页的字节位于管理范围之外。每个描述符与物理页一一对应，初始引用计数为 0。
Case 2（非法范围）：零基址、零长度、地址加法溢出、非对齐或非预期基址、超出启动映射窗口时，打印原因并 panic。
Case 3（元数据容量不足）：描述符长度计算或对齐溢出、内核与数组占满可用内存时，打印原因并 panic；数组写入和空闲区登记以全部容量检查通过为前提。

Requirements：使用现有地址转换与 PMM 接口维持系统协作。实际构建并启动内核；通过 GDB 修改 DTB 解析后的参数，分别验证正常尾页、零基址、零长度、加法溢出、映射越界、错误基址和可用空间耗尽。正常案例核对地址、页数和状态，异常案例核对对应 panic。

---

# 模块二：First-Fit 分析与 Best-Fit 实现

负责人：2413682 严苑毓。按本次采用的算法、接口与验证过程整理，并对照《提示词如何撰写》复核的最终规格。

[PROMPT]

任务：结合 kern/mm/default_pmm.c 解释 First-Fit 的初始化、登记、分配、释放和合并；在 kern/mm/best_fit_pmm.c 的 lab2 function2-2 标记处完成 Best-Fit 连续物理页管理模块，并在 pmm.c 的 init_pmm_manager() 默认选择 best_fit_pmm_manager。Best-Fit 从所有足够大的空闲块中选择长度最小者，长度相同时选择低地址块。
操作要求：直接修改实际文件；实现范围为 Best-Fit 管理器、模块内 static 辅助函数及默认管理器选择。保留 basic_check()/best_fit_check() 全文、公共 Page/free_area_t/pmm_manager 结构和宏、lab2 标记与负责人学号，沿用物理内存初始化模块提供的接口。
输出要求：交付完整源码与注册结果；说明 First-Fit 和 Best-Fit 的选择差异、复杂度及改进方向。实际编译、运行原检查与补充测试，保存命令和输出；启动输出应显示当前管理器为 best_fit_pmm_manager。

[RELY]

- Page 的 ref、flags、property、page_link；free_area_t 的 free_list、nr_free。
- PageReserved、PageProperty、SetPageProperty/ClearPageProperty、set_page_ref 和 assert。
- list_init、list_next/list_prev、list_add/list_add_before、list_del、le2page 等当前链表接口。
- pmm_manager 的 init、init_memmap、alloc_pages、free_pages、nr_free_pages、check，以及公共分配接口的转发约定。
- 原检查函数对模块内 free_list 和 nr_free 的访问及状态约定。

[GUARANTEE]

```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
```

上述函数经管理器注册供公共 PMM 接口调用；模块内部辅助函数使用 static。

[SPECIFICATION]

## best_fit_init

Pre-Condition：建立一套新的分配器状态。
Post-Condition：空闲链表为空，nr_free=0。

## best_fit_init_memmap

Pre-Condition：n>0，base 起的 n 个描述符有效且均为 Reserved，区间由初始化模块确认为可分配页。
Post-Condition：新登记页的引用计数为 0，Reserved 状态已清除；所有空闲块按地址递增排列，物理相邻块已合并。仅空闲块首页具有 PageProperty，首页 property 等于块长，nr_free 增加 n。
Case：分离区间分别保留；与既有空闲块相邻的区间形成连续大块。n=0 时触发断言。

## best_fit_alloc_pages

Pre-Condition：正常请求满足 n>0；空闲块互斥，地址顺序、首页标记、块长与 nr_free 一致。
Post-Condition（成功）：返回最小合适块起点，交付其前 n 个连续页；等长块选择低地址者。返回区间与其他持有区间互斥，各页引用计数为 0，分配首页的 PageProperty 已清除且 property=0，nr_free 减少 n。
Case 1（精确匹配）：整块从空闲集合移除；找到精确匹配后可结束搜索。
Case 2（所选块更大）：剩余部分成为地址有序的空闲块，其首页标记和长度准确。
Case 3（分配失败）：n 超过空闲总数，或总数足够但不存在 n 页连续块时返回 NULL；链表、页状态和 nr_free 保持调用前状态。
Case 4（非法请求）：n=0 时触发断言。

## best_fit_free_pages

Pre-Condition：正常调用传入 n>0 的有效区间，调用者持有其中全部页面，区间与当前空闲集合互斥；这些页的 Reserved 和 PageProperty 均已清除。
Post-Condition：回收页的引用计数为 0，nr_free 增加 n；回收区间在空闲集合中出现一次，所有物理连续邻块合并为一个块，链表保持地址递增。合并后仅新块首页保留 PageProperty 与正确块长，原首页转为内部页时 property=0。
Case：分别覆盖链首、链中、链尾插入，以及与前块、后块、双侧相邻和完全分离的情况。n=0 时触发断言。
Requirements：单次回收的计数增量恰为 n，邻块合并保持空闲页总数；完整分配后再释放可恢复原有空闲覆盖。

## best_fit_nr_free_pages 与管理器注册

Pre-Condition：管理器初始化完成，空闲集合一致。
Post-Condition：查询返回当前空闲页总数，查询前后状态一致；管理器名称为 best_fit_pmm_manager，check 绑定原 best_fit_check，pmm.c 默认选择该管理器。

Requirements（模块验证）：直接编译真实 C 源码并保留原检查。用确定性案例核对最小块、等长选择、精确/拆分、尾插、双向合并、多区间及失败状态；用独立位图核对空闲覆盖、持有区间互斥和计数。保留本次 11 项确定性与三种种子各 10,000 次随机操作，启用 ASan/UBSan，最终在 RISC-V/QEMU 中启动并执行 make grade。各条规格应可对应到断言、源码检查或运行记录。

---

# 模块三：Buddy 实现与内核接入

负责人：2413636 杨思远。按 Buddy 重写方案与最终接口整理，并对照《提示词如何撰写》复核的实现规格。

[PROMPT]

任务：在当前 ucore 物理内存框架内完成 Buddy 分配模块，交付 kern/mm/buddy_pmm.c/.h，并通过 pmm.c 和 Makefile 接入。模块按二次幂块管理空闲页，支持分裂、伙伴合并及整块回收；请求 n 页时实际交付向上取整的幂次块。
操作要求：直接修改实际文件，修改范围为 Buddy 模块及管理器、构建选择位置；沿用公共 ABI，保留物理内存初始化、Best-Fit 的最终实现和原检查函数。默认 Best-Fit，PMM_ALGO=buddy 选择 Buddy，各后端使用独立对象目录。
输出要求：交付可构建的源码与结构验证接口，说明实际占用、计数和释放约定，并记录后端构建与检查结果。

[RELY]

- pmm_manager 允许分配至少 n 页，空闲计数反映实际占用；公共 alloc_pages/free_pages 经当前管理器转发。
- Page.property、flags、ref、page_link，Reserved/Property 状态及双向循环链表接口。
- pages[0] 对应 nbase=0x80000，该页号在最大 2^18 页边界上对齐；npage-nbase 表示描述符容量。
- 物理内存初始化模块交给 init_memmap 的区间具有有效且处于 Reserved 状态的描述符；当前内核按单 hart 顺序执行。

[GUARANTEE]

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
void buddy_validate(void);
const struct pmm_manager buddy_pmm_manager;
```

管理器 check 绑定 buddy_test.c 的 buddy_check()；模块内部辅助函数使用 static。

[SPECIFICATION]

## 状态与调用约定

维护 0～18 阶空闲块，阶 o 的块含 2^o 页，块起点相对 pages 的索引按块长对齐。Page flags bit2 标记实际已分配页，分配头 property 记录实际块长。调用者成对保存 base 与原请求 n，并以原 n 释放整个块。空闲块仅首页设置 PageProperty，空闲计数按实际页数计算。

## buddy_init

Pre-Condition：建立新的 Buddy 状态。
Post-Condition：各阶链表为空，free_count=0。

## buddy_init_memmap

Pre-Condition：正常登记满足 n>0，描述符区间有效且全部为 Reserved。
Post-Condition：该区间由互斥、对齐的二次幂空闲块完整覆盖，新登记页 ref=0，free_count 增加 n；可合并的同阶伙伴已合并。
Case 1：非规则长度、分离区间及相邻区间反序登记均得到对应空闲覆盖；保留洞维持 Reserved。
Case 2：零页、无效描述符、越界或重复登记触发断言；完整区间检查是登记和清理页面状态的前提。

## buddy_alloc_pages

Pre-Condition：正常请求满足 n>0；阶链表、块状态和页计数一致。
Post-Condition（成功）：actual 为覆盖 n 的最小二次幂，返回区间按 actual 页对齐且与其他持有区间互斥；实际页均标记 bit2、ref=0，分配头 property=actual，free_count 减少 actual。其余空闲页仍由合法阶块完整覆盖。
Case 1：对应阶已有块或存在更大块时，可获得所需块；较大块分裂后的余块保持可分配。
Case 2：总量不足、缺少可用连续块或请求超过最大阶时返回 NULL，保留链表、页面状态与计数。SIZE_MAX 请求属于超阶失败。
Case 3：n=0 时触发断言。

## buddy_free_pages

Pre-Condition：正常释放传入完整分配块及原请求 n，所有实际页 ref=0。
Post-Condition：actual 页全部恢复为空闲状态，free_count 增加 actual；同阶伙伴按相对索引异或关系合并，所有可合并伙伴已合并，持有的其他区间维持原状。
Case：零页、无效或越界描述符、内部指针、重复释放、与块长不匹配的请求阶、Reserved 页面及非零引用触发断言。
Requirements：描述符范围、块对齐、实际块长及逐页状态检查通过后才回收页面；释放语义以分配头记录的 actual 和调用者保存的原 n 为依据。

## buddy_nr_free_pages

Pre-Condition：初始化完成，状态一致。
Post-Condition：返回剩余实际页总数，查询前后状态一致。

## buddy_validate

Pre-Condition：测试或调试阶段的稳定状态。
Post-Condition：一致状态正常返回；结构异常触发断言。检查涵盖链表双向关系、块范围/对齐/长度、页状态、计数及伙伴合并完整性。
Requirements：全结构慢检查由测试路径调用，日常分配释放维持阶结构及实际页状态。通过独立宿主测试和 QEMU 内核检查验证上述结果。

---

# 模块四：Buddy 独立测试

负责人：2413636 杨思远。按真实算法与本次测试整理，并对照《提示词如何撰写》复核的测试规格。

[PROMPT]

任务：为 Buddy 实现与内核接入模块设计可独立执行的宿主与内核验证，覆盖块大小、对齐、分配互斥、计数、伙伴合并、失败处理和实际内存读写。宿主案例位于 tests/buddy_host.c，内核检查位于 kern/mm/buddy_test.c，并通过管理器 check 运行。
操作要求：直接修改上述测试及 tools/test_buddy_host.py、tools/verify_c.py 等相应测试脚本，编译真实 buddy_pmm.c，沿用公共 Page/链表头和生产算法接口。用独立占用位图作为区间和计数的外部参照；生产实现与测试分别承担分配和验证职责。
输出要求：交付可执行测试、实际命令、退出状态及原始日志。正常案例输出检查结果，非法案例核对预期断言；每轮汇总使用独立 run_id，并能追溯本轮构建和执行记录。

[RELY]

- 整块分配、向上取整、传原 n 释放的调用约定，以及 buddy_validate。
- alloc_pages、free_pages、nr_free_pages、page2pa、va_pa_offset、PGSIZE 和真实 Page/链表定义。
- 宿主 Page 数组、I/O/panic 适配及 ASan/UBSan；QEMU virt 三后端选择与内存容量参数。

[GUARANTEE]

```c
// kern/mm/buddy_test.c
void buddy_check(void);
// tests/buddy_host.c
int main(int argc, char **argv);
```

同时交付宿主编译/执行脚本 tools/test_buddy_host.py、QEMU 多配置脚本 tools/verify_c.py，以及对应原始输出与运行汇总。

[SPECIFICATION]

## 宿主测试入口

Pre-Condition：每个案例从明确初始状态开始；生产分配器直接参与测试，非法操作在独立子进程执行。
Post-Condition：正常案例全部断言成立并以 0 退出；错误案例输出 assertion failed 断言失败信息并由适配层以 99 退出；测试脚本同时核对这两个条件后，将其记为该案例通过。ASan/UBSan 对执行路径完成内存和未定义行为检查。
Case 1（大小）：1～1025 页与最大阶分别核对实际幂次块长、对齐、ref、页状态、计数扣减及恢复；超阶与 SIZE_MAX 返回 NULL，失败前后状态一致。
Case 2（登记与合并）：起点 3/长度 13 和 [32,64) 共取得 45 页，保留 [16,32) 的洞；相邻 16 页反序登记形成 32 页；两个分离 8 页区间申请 16 页返回 NULL，即使空闲总量等于请求仍按连续性判断。
Case 3（随机）：固定种子执行 12,000 次操作，以独立位图核对持有区间互斥和空闲计数，最后恢复完整空闲覆盖。
Case 4（错误）：十一类输入分别验证零页、重复/内部/错误阶释放、保留/越界/NULL/非对齐描述符、重复登记及非零引用保护。任一非法操作被正常接受时，整轮测试失败。

## buddy_check

Pre-Condition：物理内存初始化模块已登记真实物理空闲页，当前管理器为 Buddy，初始可用页数足以完成案例。
Post-Condition：多种大小同时持有时区间互斥，各实际页首尾标记读回一致；逐页耗尽后再次分配返回 NULL；全量释放及 100 轮 64 页循环后，空闲页数和结构恢复。
Requirements：实际运行 64/128/256 MiB Buddy，以及三后端 128 MiB 配置，核对管理器名称、检查完成标记与串口状态；代码中的断言负责检验页面内容和计数。

## 运行脚本与汇总

Pre-Condition：每轮使用新 run_id，并从本轮实际命令收集退出状态和日志。
Post-Condition：开始状态为 RUNNING，构建失败、异常退出、超时或完成标记缺失记为 FAIL，全部案例通过后记为 PASS；汇总通过原子更新关联本轮原始日志。新一轮失败覆盖上一轮结果，读取汇总即可识别当前轮次。
Requirements：保留原始测试输出；汇总结果由退出状态、断言和完成标记共同决定。


---

# 模块五：SLUB 对象分配实现

负责人：2413681 王优。依据交接材料及最终接入接口整理的复现规格。

[PROMPT]

任务：将王优交接的 kern/mm/slub.c、slub.h 对象分配实现接入当前内核，在物理页分配器之上提供 slub_init()、slub_alloc(size) 和 slub_free(ptr)。沿用八个大小类与连续页大对象路径。
操作要求：直接修改实际工程；以交接实现为基础，保留公共 Page/PMM ABI、三种页分配器和原检查函数。在 pmm_init() 的页分配器检查之后调用 SLUB 初始化与自检。内部辅助函数使用 static。
输出要求：交付可构建源码，说明页内元数据、对象复用、对齐、大小计算及回收。通过三个 PMM 后端检查释放后的实际空闲页恢复。

[RELY]

- alloc_page()/free_page()、alloc_pages(n)/free_pages(base,n)、nr_free_pages()。
- page2pa()、va_pa_offset、PADDR()、pa2page()、PGSIZE、ROUNDUP/ROUNDDOWN、assert 与 memset。
- Best-Fit/First-Fit 取得 n 页；Buddy 向上取整，调用者保存原请求 n 并以 n 归还整个块。
- 当前为单 hart 顺序调用；交给 slub_free 的非 NULL 指针是尚未释放的完整对象起点。

[GUARANTEE]

```c
void slub_init(void);
void *slub_alloc(size_t size);
void slub_free(void *ptr);
```

实现位于 kern/mm/slub.c，接口位于 slub.h；slub_check() 由独立测试文件提供。

[SPECIFICATION]

## slub_init

Pre-Condition：页管理器已初始化，缓存处于 BSS 初始零状态或已完全归还对象的空状态。
Post-Condition：确认八个大小类缓存均为空，准备对象分配。

## slub_alloc

Pre-Condition：SLUB 与底层页分配器状态有效，按单 hart 顺序调用。
Post-Condition（成功）：返回至少覆盖 size 字节且 16 字节对齐的对象，与其他持有对象互斥。
Case 1（1～2048 字节）：向上选择 16、32、64、128、256、512、1024、2048 字节大小类。优先从已有 slab 空闲链取槽位；需要新 slab 时取得一页，记录容量、所属类、空闲链与 256 位分配位图。
Case 2（大于 2048 字节）：检查 size+元数据+页对齐运算，申请足够的连续页。首部保存原始申请页数，返回首部之后的对象区。
Case 3（失败）：零长度、大小计算溢出、页数不足或底层连续页分配失败时返回 NULL，已有对象与空闲页计数保持原状。

## slub_free

Pre-Condition：ptr 为 NULL，或是本分配器返回且仍由调用者持有的对象起点。
Post-Condition：NULL 调用正常返回。小对象清除槽位分配位并归还空闲链；slab 最后一个对象释放时摘链并归还页面。大对象按首部记录的原申请页数归还整块，使底层实际页计数恢复。
Requirements：检查 magic、对象槽位范围/边界和分配位；保留仍在使用的其他对象内容。分配位图和页管理器分别维护对象槽位与物理页状态。

---

# 模块六：SLUB 独立测试

负责人：2413681 王优；严苑毓负责在当前集成版本运行与汇总。依据交接测试及实际验证入口整理。

[PROMPT]

任务：接入王优交接的 kern/mm/slub_test.c，在当前 Best-Fit、First-Fit、Buddy 上验证真实 SLUB C 实现，并提供 tools/test_slub.py 作为独立验收入口。
操作要求：直接修改真实测试与验收脚本；保留生产算法和原有分配器检查。测试在 QEMU 中读写实际对象，核对分配区间、内容、复用和页数恢复。
输出要求：保存本轮构建、串口日志、退出状态和 run_id；三个后端全部符合断言后汇总 PASS。verify_all.py 应包含独立 SLUB 验证步骤。

[RELY]

- slub_init()/slub_alloc()/slub_free()、nr_free_pages()，以及现有 QEMU 三后端构建选择。
- 16 字节对齐、八个大小类、页首元数据和大对象连续页的调用约定。
- verify_c.run_case() 的实际构建/运行、完整最终标记、panic 检测及进程清理机制。

[GUARANTEE]

```c
void slub_check(void);
```

同时交付 tools/test_slub.py；在 pmm_init() 和 tools/verify_all.py 接入测试。原始串口与汇总存放于 evidence/slub/。

[SPECIFICATION]

## slub_check

Pre-Condition：页管理器与 SLUB 已初始化，记录 before=nr_free_pages()。
Post-Condition：下列案例全部通过；归还所有对象后 nr_free_pages() 等于 before。
Case 1（失败与空指针）：申请 0、SIZE_MAX 和超出当前空闲页容量的大小返回 NULL，页计数保持不变；slub_free(NULL) 正常返回。
Case 2（26 个大小）：覆盖 1～9000 字节及所有大小类边界。逐个检查 16 字节对齐、持有对象区间互斥，向对象全部请求字节写入不同标记并读回；逆序释放后恢复页计数。
Case 3（跨页与复用）：同时分配 140 个 64 字节对象，占用多个 slab；释放偶数位置对象后重新申请，页计数保持 occupied，说明复用已有槽位。保留的奇数位置对象内容应完整；最后全部释放恢复 before。

## test_slub.py 与总验收

Pre-Condition：本轮使用独立 run_id，运行真实内核并收集本轮串口。
Post-Condition：三个 128 MiB 后端的断言与完成标记均通过，恢复页数等于各自初始化空闲页数，汇总状态为 PASS。
Requirements：每轮先写 RUNNING；构建失败、panic、超时、缺少最终标记或计数不符时写 FAIL。内核多配置检查同时验证 SLUB 成功标记，完整组合验收包含九项。
