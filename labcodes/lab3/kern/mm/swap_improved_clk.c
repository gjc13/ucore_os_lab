#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_fifo.h>
#include <list.h>
#include <pmm.h>
#include <mmu.h>
#include <swapfs.h>

list_entry_t pra_list_head_clk;
static list_entry_t *victim = &pra_list_head_clk;

static void print_improved_clk_list(struct mm_struct *mm) {
    cprintf("now fifo list:\n");
    list_entry_t *now = pra_list_head_clk.next;
    while (now != &pra_list_head_clk) {
        struct Page *page = le2page(now, pra_page_link);
        pte_t *pte_p = get_pte(mm->pgdir, page->pra_vaddr, 0);
        volatile pte_t pte = *pte_p;
        cprintf("[pte %08x]", pte);
        assert(pte_p != NULL);
        char is_accessed = (*pte_p) & PTE_A ? 'A' : ' ';
        char is_dirty = (*pte_p) & PTE_D ? 'D' : ' ';
        char need_write_back = page->need_write_back ? 'B' : ' ';
        if (now == victim) {
            cprintf(" ** ");
        }
        cprintf("0x%08x(%c,%c,%c)->", page->pra_vaddr, is_accessed, is_dirty,
                need_write_back);
        now = now->next;
    }
    cprintf("\n");
}

static int _clk_init_mm(struct mm_struct *mm) {
    list_init(&pra_list_head_clk);
    mm->sm_priv = &pra_list_head_clk;
    print_improved_clk_list(mm);
    return 0;
}

static int _clk_map_swappable(struct mm_struct *mm, uintptr_t addr,
                              struct Page *page, int swap_in) {
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    list_entry_t *entry = &(page->pra_page_link);
    assert(entry != NULL && head != NULL);
    page->pra_vaddr = addr;
    page->need_write_back = 0;
    list_add_before(victim, entry);
    victim = victim->prev;
    print_improved_clk_list(mm);
    return 0;
}

static int _clk_swap_out_victim(struct mm_struct *mm, struct Page **ptr_page,
                                int in_tick) {
    // cprintf("[_clk_swap_out_victim] before swap out page");
    // print_improved_clk_list(mm);
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    assert(head != NULL);
    assert(in_tick == 0);
    list_entry_t *now = victim;
    assert(head->next != head);
    while (1) {
        if (now == head) {
            now = now->next;
            continue;
        }
        struct Page *page = le2page(now, pra_page_link);
        pte_t *pte_p = get_pte(mm->pgdir, page->pra_vaddr, 0);
        assert(pte_p != NULL);
        pte_t pte = *pte_p;
        if (!(pte & PTE_D) && !(pte & PTE_A)) {
            victim = now->next;
            struct Page *page = le2page(now, pra_page_link);
            (*ptr_page) = page;
            list_del(now);
            if (!page->has_backup) page->need_write_back = 1;
            // cprintf("[_clk_swap_out_victim] swap out page 0x%08x\n",
            //        page->pra_vaddr);
            // print_improved_clk_list(mm);
            return 0;
        } else if (pte & PTE_D) {
            *pte_p &= (~PTE_D);
            page->need_write_back = 1;
            tlb_invalidate(mm->pgdir, page->pra_vaddr);
        } else {
            *pte_p &= (~PTE_A);
            tlb_invalidate(mm->pgdir, page->pra_vaddr);
        }
        now = now->next;
    }
    return 0;
}

static int _clk_tick_event(struct mm_struct *mm) {
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    assert(head != NULL);
    list_entry_t *now = head->next;
    while (now != head) {
        struct Page *page = le2page(now, pra_page_link);
        if (page->need_write_back) {
            pte_t *ptep = get_pte(mm->pgdir, page->pra_vaddr, 0);
            assert(((*ptep) & PTE_P) != 0);
            if (swapfs_write((page->pra_vaddr / PGSIZE + 1) << 8, page) != 0) {
                cprintf("[_clk_tick_event] SWAP: failed to save\n");
                continue;
            } else {
                cprintf(
                    "[_clk_tick_event]swap_out: store page in vaddr 0x%x to "
                    "disk swap entry %d\n",
                    page->pra_vaddr, page->pra_vaddr / PGSIZE + 1);
                *ptep &= (~PTE_D);
                page->need_write_back = 0;
                page->has_backup = 1;
            }
            tlb_invalidate(mm->pgdir, page->pra_vaddr);
        }
        now = now->next;
    }
    return 0;
}

static int _clk_check_swap(void) {
    cprintf("write Virt Page 3 in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 4);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 1 in fifo_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 4);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 4 in fifo_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 4);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 2 in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 4);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 5 in fifo_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 2 in fifo_check_swap\n");
    cprintf("before %d\n", *(unsigned char *)0x2004);
    *(unsigned char *)0x2004 = 255;
    cprintf("after %d\n", *(unsigned char *)0x2004);
    assert(pgfault_num == 5);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 1 in fifo_check_swap\n");
    *(unsigned char *)0x1004 = 0x0a;
    assert(pgfault_num == 5);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 2 in fifo_check_swap\n");
    *(unsigned char *)0x2008 = 0x0b;
    assert(pgfault_num == 5);
    print_improved_clk_list(check_mm_struct);

    cprintf("\n-------------------------------\n");
    _clk_tick_event(check_mm_struct);
    cprintf("-------------------------------\n\n");

    cprintf("write Virt Page 3 in fifo_check_swap\n");
    *(unsigned char *)0x3004 = 0x0c;
    assert(pgfault_num == 5);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 4 in fifo_check_swap\n");
    *(unsigned char *)0x4004 = 0x0d;
    assert(pgfault_num == 6);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 5 in fifo_check_swap\n");
    *(unsigned char *)0x5004 = 0x0e;
    assert(pgfault_num == 6);
    print_improved_clk_list(check_mm_struct);

    cprintf("write Virt Page 1 in fifo_check_swap\n");
    *(unsigned char *)0x1008 = 0x0a;
    assert(pgfault_num == 6);
    print_improved_clk_list(check_mm_struct);
    return 0;
}

static int _clk_init(void) { return 0; }

static int _clk_set_unswappable(struct mm_struct *mm, uintptr_t addr) {
    return 0;
}

struct swap_manager swap_manager_improved_clk = {
    .name = "improved clock swap manager",
    .init = &_clk_init,
    .init_mm = &_clk_init_mm,
    .tick_event = &_clk_tick_event,
    .map_swappable = &_clk_map_swappable,
    .set_unswappable = &_clk_set_unswappable,
    .swap_out_victim = &_clk_swap_out_victim,
    .check_swap = &_clk_check_swap,
};
