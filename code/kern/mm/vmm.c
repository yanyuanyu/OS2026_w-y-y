#include <vmm.h>
#include <sync.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <riscv.h>
#include <kmalloc.h>

/*
  vmm design include two parts: mm_struct (mm) & vma_struct (vma)
  mm is the memory manager for the set of continuous virtual memory
  area which have the same PDT. vma is a continuous virtual memory area.
  There a linear link list for vma & a redblack link list for vma in mm.
---------------
  mm related functions:
   golbal functions
     struct mm_struct * mm_create(void)
     void mm_destroy(struct mm_struct *mm)
     int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
--------------
  vma related functions:
   global functions
     struct vma_struct * vma_create (uintptr_t vm_start, uintptr_t vm_end,...)
     void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
     struct vma_struct * find_vma(struct mm_struct *mm, uintptr_t addr)
   local functions
     inline void check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
---------------
   check correctness functions
     void check_vmm(void);
     void check_vma_struct(void);
     void check_pgfault(void);
*/

static void check_vmm(void);
static void check_vma_struct(void);

// mm_create -  alloc a mm_struct & initialize it.
struct mm_struct *
mm_create(void)
{
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));

    if (mm != NULL)
    {
        list_init(&(mm->mmap_list));
        mm->mmap_cache = NULL;
        mm->pgdir = NULL;
        mm->map_count = 0;

        mm->sm_priv = NULL;

        set_mm_count(mm, 0);
        lock_init(&(mm->mm_lock));
    }
    return mm;
}

// vma_create - alloc a vma_struct & initialize it. (addr range: vm_start~vm_end)
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags)
{
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));

    if (vma != NULL)
    {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
    }
    return vma;
}

// find_vma - find a vma  (vma->vm_start <= addr <= vma_vm_end)
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr)
{
    struct vma_struct *vma = NULL;
    if (mm != NULL)
    {
        vma = mm->mmap_cache;
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr))
        {
            bool found = 0;
            list_entry_t *list = &(mm->mmap_list), *le = list;
            while ((le = list_next(le)) != list)
            {
                vma = le2vma(le, list_link);
                if (vma->vm_start <= addr && addr < vma->vm_end)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                vma = NULL;
            }
        }
        if (vma != NULL)
        {
            mm->mmap_cache = vma;
        }
    }
    return vma;
}

// check_vma_overlap - check if vma1 overlaps vma2 ?
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
{
    assert(prev->vm_start < prev->vm_end);
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end);
}

// insert_vma_struct -insert vma in mm's list link
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
{
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list);
    list_entry_t *le_prev = list, *le_next;

    list_entry_t *le = list;
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *mmap_prev = le2vma(le, list_link);
        if (mmap_prev->vm_start > vma->vm_start)
        {
            break;
        }
        le_prev = le;
    }

    le_next = list_next(le_prev);

    /* check overlap */
    if (le_prev != list)
    {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    if (le_next != list)
    {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    vma->vm_mm = mm;
    list_add_after(le_prev, &(vma->list_link));

    mm->map_count++;
}

// mm_destroy - free mm and mm internal fields
void mm_destroy(struct mm_struct *mm)
{
    assert(mm_count(mm) == 0);

    list_entry_t *list = &(mm->mmap_list), *le;
    while ((le = list_next(list)) != list)
    {
        list_del(le);
        kfree(le2vma(le, list_link)); // kfree vma
    }
    kfree(mm); // kfree mm
    mm = NULL;
}

int mm_map(struct mm_struct *mm, uintptr_t addr, size_t len, uint32_t vm_flags,
           struct vma_struct **vma_store)
{
    if (len == 0 || addr + len < addr) return -E_INVAL;
    uintptr_t start = ROUNDDOWN(addr, PGSIZE), end = ROUNDUP(addr + len, PGSIZE);
    if (!USER_ACCESS(start, end))
    {
        return -E_INVAL;
    }

    assert(mm != NULL);

    int ret = -E_INVAL;

    struct vma_struct *vma;
    list_entry_t *head = &mm->mmap_list;
    for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
        struct vma_struct *existing = le2vma(le, list_link);
        if (start < existing->vm_end && existing->vm_start < end) goto out;
    }
    ret = -E_NO_MEM;

    if ((vma = vma_create(start, end, vm_flags)) == NULL)
    {
        goto out;
    }
    insert_vma_struct(mm, vma);
    if (vma_store != NULL)
    {
        *vma_store = vma;
    }
    ret = 0;

out:
    return ret;
}

/* A: 三阶段事务：复制全部 VMA -> 预分配全部页表 -> 提交 COW。
 * to 必须为空，from 的 mm_lock 由 copy_mm 持有；提交期间本核关中断。
 * 失败清空 to 的 VMA 与下级页表，根表仍由调用者 put_pgdir 释放。 */
//lab5 2413636：dup_mmap
int dup_mmap(struct mm_struct *to, struct mm_struct *from)
{
    assert(to != NULL && from != NULL && to != from);
    assert(to->pgdir != NULL && from->pgdir != NULL);
    assert(to->map_count == 0 && list_empty(&to->mmap_list));
    int ret = -E_NO_MEM;
    list_entry_t *head = &from->mmap_list, *le;
    for (le = list_next(head); le != head; le = list_next(le)) {
        struct vma_struct *src = le2vma(le, list_link);
        struct vma_struct *dst = vma_create(src->vm_start, src->vm_end, src->vm_flags);
        if (dst == NULL) goto fail;
        insert_vma_struct(to, dst);
    }
    head = &to->mmap_list;
    for (le = list_next(head); le != head; le = list_next(le)) {
        struct vma_struct *vma = le2vma(le, list_link);
        ret = copy_range_prepare(to->pgdir, from->pgdir, vma->vm_start, vma->vm_end);
        if (ret != 0) goto fail;
    }
    bool intr_flag;
    local_intr_save(intr_flag);
    for (le = list_next(head); le != head; le = list_next(le)) {
        struct vma_struct *vma = le2vma(le, list_link);
        ret = copy_range(to->pgdir, from->pgdir, vma->vm_start, vma->vm_end, 1);
        assert(ret == 0); /* 提交阶段没有分配失败点 */
    }
    local_intr_restore(intr_flag);
    return 0;
fail:
    exit_mmap(to);
    head = &to->mmap_list;
    while (!list_empty(head)) {
        le = list_next(head); list_del(le); kfree(le2vma(le, list_link));
    }
    to->map_count = 0; to->mmap_cache = NULL;
    return ret;
}

/* 仅供已持 mm 锁且本核关中断的路径使用。真正只读页绝不升级。 */
static int cow_break_locked(struct mm_struct *mm, uintptr_t addr)
{
    struct vma_struct *vma = find_vma(mm, addr);
    if (vma == NULL || !(vma->vm_flags & VM_WRITE) ||
        ((vma->vm_flags & VM_STACK) && addr < vma->vm_start + PGSIZE))
        return -E_INVAL;
    uintptr_t la = ROUNDDOWN(addr, PGSIZE);
    pte_t *pte = get_pte(mm->pgdir, la, 0);
    if (pte == NULL || (*pte & (PTE_V | PTE_U | PTE_R | PTE_COW | PTE_W)) !=
        (PTE_V | PTE_U | PTE_R | PTE_COW)) return -E_INVAL;
    struct Page *old = pte2page(*pte);
    assert(page_ref(old) > 0);
    uint32_t perm = ((*pte & 0x3ff) | PTE_W) & ~PTE_COW;
    if (page_ref(old) == 1) {
        *pte = pte_create(page2ppn(old), perm);
        tlb_invalidate(mm->pgdir, la);
        return 0;
    }
    struct Page *copy = alloc_page();
    if (copy == NULL) return -E_NO_MEM;
    memcpy(page2kva(copy), page2kva(old), PGSIZE);
    int ret = page_insert(mm->pgdir, copy, la, perm);
    if (ret != 0) free_page(copy);
    return ret;
}

//lab5 2413636：cow_pgfault
int cow_pgfault(struct mm_struct *mm, uintptr_t addr)
{
    if (mm == NULL || mm->pgdir == NULL || !USER_ACCESS(addr, addr + 1))
        return -E_INVAL;
    lock_mm(mm);
    bool intr_flag;
    local_intr_save(intr_flag);
    int ret = cow_break_locked(mm, addr);
    local_intr_restore(intr_flag);
    unlock_mm(mm);
    return ret;
}



void exit_mmap(struct mm_struct *mm)
{
    assert(mm != NULL && mm_count(mm) == 0);
    pde_t *pgdir = mm->pgdir;
    list_entry_t *list = &(mm->mmap_list), *le = list;
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *vma = le2vma(le, list_link);
        unmap_range(pgdir, vma->vm_start, vma->vm_end);
    }
    while ((le = list_next(le)) != list)
    {
        struct vma_struct *vma = le2vma(le, list_link);
        exit_range(pgdir, vma->vm_start, vma->vm_end);
    }
}

bool copy_from_user(struct mm_struct *mm, void *dst, const void *src, size_t len, bool writable) {
    if(len==0)return 1;
    uintptr_t addr=(uintptr_t)src;
    if(!user_mem_check(mm,addr,len,writable))return 0;
    if(mm==NULL){memcpy(dst,src,len);return 1;}
    char *out=dst;
    while(len) {
        pte_t *p=get_pte(mm->pgdir,addr,0);
        if(p==NULL || (*p&(PTE_V|PTE_U|PTE_R))!=(PTE_V|PTE_U|PTE_R))return 0;
        size_t n=PGSIZE-PGOFF(addr);if(n>len)n=len;
        memcpy(out,(char *)page2kva(pte2page(*p))+PGOFF(addr),n);
        out+=n;addr+=n;len-=n;
    }
    return 1;
}

bool copy_to_user(struct mm_struct *mm, void *dst, const void *src, size_t len)
{
    if (mm == NULL) {
        if (!user_mem_check(NULL, (uintptr_t)dst, len, 1)) return 0;
        memcpy(dst, src, len);
        return 1;
    }
    if (mm->pgdir == NULL) return 0;
    lock_mm(mm);
    bool intr_flag, ok = 0;
    local_intr_save(intr_flag);
    uintptr_t addr = (uintptr_t)dst;
    const char *source = src;
    if (!user_mem_check(mm, addr, len, 1)) goto out;
    /* 先经 mm 的叶子 PTE 校验/拆分，再从新页的内核直映地址写入。
     * 不能直接用旧 page2kva 写共享页，也不依赖当前 satp 恰好等于 mm。 */
    while (len > 0) {
        pte_t *pte = get_pte(mm->pgdir, addr, 0);
        if (pte == NULL || (*pte & (PTE_V | PTE_U | PTE_R)) !=
            (PTE_V | PTE_U | PTE_R)) goto out;
        if (*pte & PTE_COW) {
            if (cow_break_locked(mm, addr) != 0) goto out;
        }
        if (!(*pte & PTE_W)) goto out;
        size_t n = PGSIZE - PGOFF(addr);
        if (n > len) n = len;
        memcpy((char *)page2kva(pte2page(*pte)) + PGOFF(addr), source, n);
        addr += n; source += n; len -= n;
    }
    ok = 1;
out:
    local_intr_restore(intr_flag);
    unlock_mm(mm);
    return ok;
}

// vmm_init - initialize virtual memory management
//          - now just call check_vmm to check correctness of vmm
void vmm_init(void)
{
    check_vmm();
}

// check_vmm - check correctness of vmm
static void
check_vmm(void)
{
    // size_t nr_free_pages_store = nr_free_pages();

    check_vma_struct();
    // check_pgfault();

    cprintf("check_vmm() succeeded.\n");
}

static void
check_vma_struct(void)
{
    // size_t nr_free_pages_store = nr_free_pages();

    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = 10, step2 = step1 * 10;

    int i;
    for (i = step1; i >= 1; i--)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    for (i = step1 + 1; i <= step2; i++)
    {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 1; i <= step2; i++)
    {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    for (i = 5; i <= 5 * step2; i += 5)
    {
        struct vma_struct *vma1 = find_vma(mm, i);
        assert(vma1 != NULL);
        struct vma_struct *vma2 = find_vma(mm, i + 1);
        assert(vma2 != NULL);
        struct vma_struct *vma3 = find_vma(mm, i + 2);
        assert(vma3 == NULL);
        struct vma_struct *vma4 = find_vma(mm, i + 3);
        assert(vma4 == NULL);
        struct vma_struct *vma5 = find_vma(mm, i + 4);
        assert(vma5 == NULL);

        assert(vma1->vm_start == i && vma1->vm_end == i + 2);
        assert(vma2->vm_start == i && vma2->vm_end == i + 2);
    }

    for (i = 4; i >= 0; i--)
    {
        struct vma_struct *vma_below_5 = find_vma(mm, i);
        if (vma_below_5 != NULL)
        {
            cprintf("vma_below_5: i %x, start %x, end %x\n", i, vma_below_5->vm_start, vma_below_5->vm_end);
        }
        assert(vma_below_5 == NULL);
    }

    mm_destroy(mm);

    cprintf("check_vma_struct() succeeded!\n");
}
bool user_mem_check(struct mm_struct *mm, uintptr_t addr, size_t len, bool write)
{
    if (mm != NULL)
    {
        if (!USER_ACCESS(addr, addr + len))
        {
            return 0;
        }
        struct vma_struct *vma;
        uintptr_t start = addr, end = addr + len;
        while (start < end)
        {
            if ((vma = find_vma(mm, start)) == NULL || start < vma->vm_start)
            {
                return 0;
            }
            if (!(vma->vm_flags & ((write) ? VM_WRITE : VM_READ)))
            {
                return 0;
            }
            if (write && (vma->vm_flags & VM_STACK))
            {
                if (start < vma->vm_start + PGSIZE)
                { // check stack start & size
                    return 0;
                }
            }
            start = vma->vm_end;
        }
        return 1;
    }
    return KERN_ACCESS(addr, addr + len);
}
