/* Controlled stale-write-reference model using real ucore PTEs/pages.
 * This is NOT a reproduction of Linux CVE-2016-5195 (/proc + madvise).
 * The vulnerable write exists only in this injected test fixture.
 */
#include <vmm.h>
#include <pmm.h>
#include <kmalloc.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

static struct mm_struct *new_mm(void) {
    struct mm_struct *m=mm_create();assert(m);
    struct Page *p=alloc_page();assert(p);
    m->pgdir=page2kva(p);memcpy(m->pgdir,boot_pgdir_va,PGSIZE);
    assert(mm_map(m,USERBASE,PGSIZE,VM_READ|VM_WRITE,NULL)==0);
    return m;
}
static void destroy(struct mm_struct *m) {
    exit_mmap(m);free_page(kva2page(m->pgdir));mm_destroy(m);
}
static void one_case(bool fixed) {
    struct mm_struct *parent=new_mm(),*child=new_mm();
    struct Page *p=alloc_page();assert(p);memset(page2kva(p),0,PGSIZE);
    *(int *)page2kva(p)=7;
    assert(page_insert(parent->pgdir,p,USERBASE,PTE_U|PTE_R|PTE_COW)==0);
    assert(page_insert(child->pgdir,p,USERBASE,PTE_U|PTE_R|PTE_COW)==0);
    // Logical T1: pin the page and retain a kernel alias before the COW transition.
    page_ref_inc(p);int *stale=page2kva(p);
    // Logical T2: resolve the writer's COW mapping; the old alias is now stale.
    assert(cow_pgfault(parent,USERBASE)==0);
    if(fixed) {
        // Re-resolve after COW and use the current mapping, never the old pin.
        struct Page *fresh=get_page(parent->pgdir,USERBASE,NULL);
        assert(fresh!=p);*(int *)page2kva(fresh)=99;
    } else *stale=99;
    int a=*(int *)page2kva(get_page(parent->pgdir,USERBASE,NULL));
    int b=*(int *)page2kva(get_page(child->pgdir,USERBASE,NULL));
    if(fixed){assert(a==99 && b==7);cprintf("[DirtyCOW model] fixed re-resolution preserves sibling PASS\n");}
    else {assert(a==7 && b==99);cprintf("[DirtyCOW model] vulnerable stale alias corrupts sibling (expected) PASS\n");}
    assert(page_ref_dec(p)==1);
    destroy(parent);destroy(child);
}
void c_test_dirtycow_model(void) {
    slob_reclaim();size_t before=nr_free_pages();
    one_case(0);one_case(1);
    slob_reclaim();assert(before==nr_free_pages());
    cprintf("DIRTYCOW_MODEL_ALL_PASS (controlled ucore model, not Linux CVE reproduction)\n");
}
