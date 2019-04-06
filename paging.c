#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "paging.h"
#include "fs.h"

static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

/* Allocate eight consecutive disk blocks.
 * Save the content of the physical page in the pte
 * to the disk blocks and save the block-id into the
 * pte.
 */
void
swap_page_from_pte(pte_t *pte)
{	
	// begin_op();
	//cprintf("Attempting to swap \n");
	if((*pte & PTE_P) == 0) return;
	uint start = balloc_page(1);
	//cprintf("In swap function after ballocing\n");
	char *temp = (char *) PTE_ADDR(*pte);
	//cprintf("Attempting to write to disk\n");
	
	write_page_to_disk(1,P2V(temp),start);
	
	// end_op();
	//cprintf("Written to disk\n");
	// uint ppn = *pte & 0xfffff000;
	uint flags = *pte & 0xfff;
	flags = flags & ~PTE_P; //Set present flag to 0
	flags = flags | PTE_O; //Disk flag represented by 11th bit.
	*pte = (start<<12) | flags;

	lcr3(V2P(myproc()->pgdir)); //Flush TLB
	//cprintf("In swap page from PTE after writing\n");
	kfree(P2V(temp)); // Free the page.
	//cprintf("K-free succesful\n");

	

}

/* Select a victim and swap the contents to the disk.
 */
int
swap_page(pde_t *pgdir)
{
	pte_t *pte = select_a_victim(pgdir);
	//cprintf("Victim is %d\n",*pte);
	swap_page_from_pte(pte);
	//panic("swap_page is not implemented");
	return 1;
}

/* Map a physical page to the virtual address addr.
 * If the page table entry points to a swapped block
 * restore the content of the page from the swapped
 * block and free the swapped block.
 */
void
map_address(pde_t *pgdir, uint addr)
{
	begin_op();
	
	char *page = kalloc();
	//cprintf("%d page\n",page);
	pte_t *pte = walkpgdir(pgdir,(char *)addr,1);
	
	if (pte == 0){
		panic("YO\n\n");
	}

	uint blockid = getswappedblk(pgdir, *pte);

	if(page == 0){
		swap_page(pgdir);
		page = kalloc();
		//cprintf("page is %d",*page);
		if(page == 0) panic("DED\n");
		if(*pte & PTE_O) {
			//cprintf("Attempting to read\n");
			read_page_from_disk(1,(char *)page,blockid);
			//cprintf("Read\n");
			bfree_page(1,blockid);
			*pte = V2P(page) | (*pte & 0xfff);
			*pte = *pte | PTE_P;
			*pte = *pte & (~PTE_O);
		}
		else{
			memset(page,0,PGSIZE);
			*pte = V2P(page) | PTE_P | PTE_U | PTE_W;
		}		
	}
	else{
		if(*pte & PTE_O){
			//cprintf("Attempting to read\n");
			read_page_from_disk(1,(char *)page,blockid);
			//cprintf("Read\n");
			bfree_page(1,blockid);
			*pte = V2P(page) | (*pte & 0xfff);
			*pte = *pte | PTE_P;
			*pte = *pte & (~PTE_O);
		}
		else{
			memset(page,0,PGSIZE);
			*pte = V2P(page) | PTE_P | PTE_U | PTE_W;
		}	
	}
	end_op();
}

/* page fault handler */
void
handle_pgfault()
{
	unsigned addr;
	struct proc *curproc = myproc();

	asm volatile ("movl %%cr2, %0 \n\t" : "=r" (addr));
	addr &= ~0xfff;
	map_address(curproc->pgdir, addr);
}
