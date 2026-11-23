#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <kmonitor.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// Lab4 process switching is placed after the Lab5 implementation.

/* A/B 共用前置：fork 依赖 PCB 分配，先初始化 Lab5 新增的进程树字段。
 * B 后续 proc_init/kernel_thread 可以直接复用。 */
//lab5 2413681：alloc_proc
static struct proc_struct *alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(*proc));
    if (proc != NULL) {
        memset(proc, 0, sizeof(*proc));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->pgdir = boot_pgdir_pa;
        list_init(&proc->list_link);
        list_init(&proc->hash_link);
    }
    return proc;
}


// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}






// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

// Lab5 B: create a kernel thread through the common fork path.
//lab5 2413681：kernel_thread
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

/* A: CLONE_VM 共享整个 mm；普通 fork 独立 mm/页表并共享 COW 数据页。 */
//lab5 2413636：copy_mm
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    struct mm_struct *oldmm = current->mm, *mm;
    if (oldmm == NULL) return 0; /* 内核线程保持 boot_pgdir */
    if (clone_flags & CLONE_VM) {
        mm = oldmm;
    } else {
        mm = mm_create();
        if (mm == NULL) return -E_NO_MEM;
        int ret = setup_pgdir(mm);
        if (ret != 0) { mm_destroy(mm); return ret; }
        lock_mm(oldmm);
        ret = dup_mmap(mm, oldmm);
        unlock_mm(oldmm);
        if (ret != 0) {
            /* dup_mmap 已撤销 VMA/下级页表，根表在本层回收。 */
            put_pgdir(mm); mm_destroy(mm); return ret;
        }
    }
    mm_count_inc(mm);
    proc->mm = mm;
    proc->pgdir = PADDR(mm->pgdir);
    return 0;
}

//lab5 2413636：copy_thread
static void copy_thread(struct proc_struct *proc, uintptr_t stack, struct trapframe *tf)
{
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *proc->tf = *tf;
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = stack != 0 ? stack : (uintptr_t)proc->tf;
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}



// Lab5 B: load a linked-in ELF image into a fresh address space.
//lab5 2413681：load_icode
static int
load_icode(unsigned char *binary, size_t size)
{
    int ret = -E_INVAL_ELF;
    struct mm_struct *mm = NULL;
    if (size < sizeof(struct elfhdr)) return ret;
    struct elfhdr *elf = (struct elfhdr *)binary;
    if (elf->e_magic != ELF_MAGIC || elf->e_phentsize != sizeof(struct proghdr) ||
        elf->e_phoff > size || elf->e_phnum > (size - elf->e_phoff) / sizeof(struct proghdr)) return ret;
    if ((mm = mm_create()) == NULL) return -E_NO_MEM;
    if ((ret = setup_pgdir(mm)) != 0) goto bad_mm;
    bool entry_valid = 0;
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff);
    for (unsigned i = 0; i < elf->e_phnum; i++, ph++) {
        if (ph->p_type != ELF_PT_LOAD || ph->p_memsz == 0) continue;
        ret = -E_INVAL_ELF;
        if (ph->p_filesz > ph->p_memsz || ph->p_offset > size ||
            ph->p_filesz > size - ph->p_offset || ph->p_va + ph->p_memsz < ph->p_va ||
            !USER_ACCESS(ph->p_va, ph->p_va + ph->p_memsz)) goto bad_map;
        if ((ph->p_flags & ELF_PF_X) && elf->e_entry >= ph->p_va && elf->e_entry < ph->p_va + ph->p_memsz) entry_valid = 1;
        uint32_t vm = 0, perm = PTE_U;
        if (ph->p_flags & ELF_PF_R) { vm |= VM_READ; perm |= PTE_R; }
        if (ph->p_flags & ELF_PF_W) { vm |= VM_WRITE; perm |= PTE_W | PTE_R; }
        if (ph->p_flags & ELF_PF_X) { vm |= VM_EXEC; perm |= PTE_X; }
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm, NULL)) != 0) goto bad_map;
        uintptr_t start = ROUNDDOWN(ph->p_va, PGSIZE), end = ROUNDUP(ph->p_va + ph->p_memsz, PGSIZE);
        for (uintptr_t a = start; a < end; a += PGSIZE) {
            struct Page *page = pgdir_alloc_page(mm->pgdir, a, perm);
            if (page == NULL) { ret = -E_NO_MEM; goto bad_map; }
            memset(page2kva(page), 0, PGSIZE);
            uintptr_t lo = a > ph->p_va ? a : ph->p_va;
            uintptr_t hi = a + PGSIZE < ph->p_va + ph->p_filesz ? a + PGSIZE : ph->p_va + ph->p_filesz;
            if (hi > lo) memcpy((char *)page2kva(page) + (lo - a), binary + ph->p_offset + (lo - ph->p_va), hi - lo);
        }
    }
    if (!entry_valid) { ret = -E_INVAL_ELF; goto bad_map; }
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, VM_READ | VM_WRITE | VM_STACK, NULL)) != 0) goto bad_map;
    for (int i = 1; i <= 4; i++) {
        struct Page *page = pgdir_alloc_page(mm->pgdir, USTACKTOP - i * PGSIZE, PTE_U | PTE_R | PTE_W);
        if (page == NULL) { ret = -E_NO_MEM; goto bad_map; }
        memset(page2kva(page), 0, PGSIZE);
    }
    mm_count_inc(mm);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);
    lsatp(current->pgdir);
    asm volatile("sfence.vma" ::: "memory");
    struct trapframe *tf = current->tf;
    memset(tf, 0, sizeof(*tf));
    tf->gpr.sp = USTACKTOP;
    tf->epc = elf->e_entry;
    tf->status = (read_csr(sstatus) & ~(SSTATUS_SPP | SSTATUS_SIE)) | SSTATUS_SPIE;
    return 0;
bad_map:
    exit_mmap(mm);
    put_pgdir(mm);
bad_mm:
    mm_destroy(mm);
    return ret;
}

//lab5 2413681：do_execve
int
do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    char local[PROC_NAME_LEN + 1] = {0};
    unsigned char *image = binary;
    struct mm_struct *mm = current->mm;
    if (len > PROC_NAME_LEN) len = PROC_NAME_LEN;
    lock_mm(mm);
    if (!copy_from_user(mm, local, name, len, 0)) { unlock_mm(mm); return -E_INVAL; }
    if (size == 0 || !user_mem_check(mm, (uintptr_t)binary, size, 0)) { unlock_mm(mm); return -E_INVAL; }
    if (mm != NULL) {
        image = kmalloc(size);
        if (image == NULL) { unlock_mm(mm); return -E_NO_MEM; }
        if (!copy_from_user(mm, image, binary, size, 0)) { kfree(image); unlock_mm(mm); return -E_INVAL; }
    }
    unlock_mm(mm);
    local[len] = '\0';
    if (mm != NULL) {
        lsatp(boot_pgdir_pa);
        asm volatile("sfence.vma" ::: "memory");
        if (mm_count_dec(mm) == 0) { exit_mmap(mm); put_pgdir(mm); mm_destroy(mm); }
        current->mm = NULL;
        current->pgdir = boot_pgdir_pa;
    }
    int ret = load_icode(image, size);
    if (mm != NULL) kfree(image);
    if (ret != 0) { do_exit(ret); return ret; }
    set_proc_name(current, local);
    return 0;
}

//lab5 2413681：do_wait
int
do_wait(int pid, int *code_store)
{
    if (pid < 0) return -E_BAD_PROC;
    if (code_store != NULL && !user_mem_check(current->mm, (uintptr_t)code_store, sizeof(*code_store), 1)) return -E_INVAL;
    for (;;) {
        bool intr_flag;
        local_intr_save(intr_flag);
        bool has_child = 0;
        struct proc_struct *p;
        for (p = current->cptr; p != NULL; p = p->optr) {
            if (pid != 0 && p->pid != pid) continue;
            has_child = 1;
            if (p->state == PROC_ZOMBIE) {
                int exit_code = p->exit_code;
                local_intr_restore(intr_flag);
                if (code_store != NULL && !copy_to_user(current->mm, code_store, &exit_code, sizeof(exit_code))) return -E_INVAL;
                local_intr_save(intr_flag);
                assert(p->state == PROC_ZOMBIE && p->parent == current);
                unhash_proc(p);
                remove_links(p);
                local_intr_restore(intr_flag);
                put_kstack(p);
                kfree(p);
                return 0;
            }
        }
        if (!has_child) { local_intr_restore(intr_flag); return -E_BAD_PROC; }
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        local_intr_restore(intr_flag);
        schedule();
        if (current->flags & PF_EXITING) do_exit(-E_KILLED);
    }
}

//lab5 2413681：do_exit
int
do_exit(int error_code)
{
    if (current == idleproc || current == initproc) panic("protected process exit\n");
    struct mm_struct *mm = current->mm;
    if (mm != NULL) {
        lsatp(boot_pgdir_pa);
        asm volatile("sfence.vma" ::: "memory");
        if (mm_count_dec(mm) == 0) { exit_mmap(mm); put_pgdir(mm); mm_destroy(mm); }
        current->mm = NULL;
        current->pgdir = boot_pgdir_pa;
    }
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;
    bool intr_flag;
    local_intr_save(intr_flag);
    if (current->parent->wait_state == WT_CHILD) wakeup_proc(current->parent);
    while (current->cptr != NULL) {
        struct proc_struct *p = current->cptr;
        current->cptr = p->optr;
        if (p->optr != NULL) p->optr->yptr = NULL;
        p->parent = initproc; p->yptr = NULL; p->optr = initproc->cptr;
        if (p->optr != NULL) p->optr->yptr = p;
        initproc->cptr = p;
        if (p->state == PROC_ZOMBIE && initproc->wait_state == WT_CHILD) wakeup_proc(initproc);
    }
    local_intr_restore(intr_flag);
    schedule();
    panic("zombie process resumed\n");
}


/* 资源准备完毕才发布子进程；PID/树/哈希/就绪状态在同一临界区更新。 */
//lab5 2413636：do_fork
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    if (nr_process >= MAX_PROCESS) return -E_NO_FREE_PROC;
    if (current == NULL || tf == NULL) return -E_INVAL;
    struct proc_struct *proc = alloc_proc();
    if (proc == NULL) return -E_NO_MEM;
    proc->parent = current;
    int ret = setup_kstack(proc);
    if (ret != 0) goto bad_proc;
    ret = copy_mm(clone_flags, proc);
    if (ret != 0) goto bad_stack;
    copy_thread(proc, stack, tf);
    bool intr_flag;
    local_intr_save(intr_flag);
    /* lock_mm 竞争时可能调度，发布前重新检查容量。 */
    if (nr_process >= MAX_PROCESS) {
        local_intr_restore(intr_flag);
        ret = -E_NO_FREE_PROC;
        goto bad_mm;
    }
    proc->pid = get_pid();
    hash_proc(proc);
    set_links(proc); /* Lab5 不再只挂 proc_list，还要维护兄弟/子女关系 */
    wakeup_proc(proc);
    local_intr_restore(intr_flag);
    return proc->pid;
bad_mm:
    if (proc->mm != NULL && mm_count_dec(proc->mm) == 0) {
        exit_mmap(proc->mm);
        put_pgdir(proc->mm);
        mm_destroy(proc->mm);
    }
bad_stack:
    put_kstack(proc);
bad_proc:
    kfree(proc);
    return ret;
}


//lab5 function4 2413682：进程控制。
//lab5 2413682：do_yield
int do_yield(void) {
    current->need_resched=1;
    return 0;
}
//lab5 2413682：do_kill
int do_kill(int pid) {
    bool flag;
    local_intr_save(flag);
    struct proc_struct *p=find_proc(pid);
    int ret=-E_INVAL;
    if (p != NULL && p != idleproc && p != initproc &&
        p->state != PROC_ZOMBIE && !(p->flags & PF_EXITING)) {
        p->flags |= PF_EXITING;
        if (p->state == PROC_SLEEPING && (p->wait_state & WT_INTERRUPTED))
            wakeup_proc(p);
        ret=0;
    }
    local_intr_restore(flag);
    return ret;
}


#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


// Lab5 B: start the first user process from a kernel thread.
//lab5 2413681：kernel_execve
static int
kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    int ret = do_execve(name, strlen(name), binary, size);
    if (ret == 0) { intr_disable(); forkrets(current->tf); }
    return ret;
}

//lab5 2413681：user_main
static int
user_main(void *arg)
{
#ifdef TEST
    KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    KERNEL_EXECVE(exit);
#endif
    panic("user program failed to start\n");
}

//lab5 2413681：init_main
static int
init_main(void *arg)
{
    slob_reclaim();
    size_t free_before = nr_free_pages();
    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0) panic("cannot create user process\n");
    while (do_wait(0, NULL) == 0) { }
    cprintf("all user-mode processes have quit.\n");
    slob_reclaim();
    cprintf("memory balance: before=%lu after=%lu\n", free_before, nr_free_pages());
    assert(nr_free_pages() == free_before);
    cprintf("init check memory pass.\n");
    kmonitor(NULL);
    for (;;) { current->need_resched = 1; schedule(); }
    return 0;
}

//lab5 2413681：proc_init
void
proc_init(void)
{
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++) list_init(&hash_list[i]);
    if ((idleproc = alloc_proc()) == NULL) panic("cannot allocate idle process\n");
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;
    current = idleproc;
    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0 || (initproc = find_proc(pid)) == NULL) panic("cannot create init process\n");
    set_proc_name(initproc, "init");
}




//lab5 2413682：cpu_idle
void cpu_idle(void) {
    while (1) if (current->need_resched) schedule();
}

//lab5 2413682：proc_run
void proc_run(struct proc_struct *proc) {
    assert(proc != NULL && proc->state == PROC_RUNNABLE);
    if (proc != current) {
        bool flag;
        local_intr_save(flag);
        struct proc_struct *prev=current;
        current=proc;
        write_csr(satp, ((uintptr_t)SATP_MODE_SV39 << 60) |
                       (proc->pgdir >> RISCV_PGSHIFT));
        flush_tlb();
        switch_to(&prev->context,&proc->context);
        local_intr_restore(flag);
    }
}


