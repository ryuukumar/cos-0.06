#include <stddef.h>
#include <stdint.h>

#include <mvmng.h>
#include <mpmng.h>
#include <mmain.h>

#include <stdio.h>
#include <sys/drivers/timer.h>

#include <sys/regs.h>

extern "C" {
	extern void write_cr3(uint32_t);
	extern uint32_t read_cr3(void);
	extern void irq_install_handler(int irq, void (*handler)(struct regs *r));
}

void set_bit_pdir (uint32_t bit, page_dir_entry_t* p_entry) {*p_entry |= bit;}
void clr_bit_pdir (uint32_t bit, page_dir_entry_t* p_entry) {*p_entry &= ~bit;}
uint32_t get_bit_pdir (uint32_t bit, page_dir_entry_t* p_entry) {return *p_entry & bit;}
// note: above will return 0 if clear, else won't return 1
void set_addr_pdir (uint32_t addr, page_dir_entry_t* p_entry) {*p_entry = (*p_entry & ~dframe) | addr;}
uint32_t get_addr_pdir (page_dir_entry_t* p_entry) {return *p_entry & dframe;}

void set_bit_ptb (uint32_t bit, page_table_entry_t* p_entry) {*p_entry |= bit;}
void clr_bit_ptb (uint32_t bit, page_table_entry_t* p_entry) {*p_entry &= ~bit;}
uint32_t get_bit_ptb (uint32_t bit, page_table_entry_t* p_entry) {return *p_entry & bit;}
// note: above will return 0 if clear, else won't return 1
void set_addr_ptb (uint32_t addr, page_table_entry_t* p_entry) {*p_entry = (*p_entry & 0x1FFF) | addr;}
uint32_t get_addr_ptb (page_table_entry_t* p_entry) {return *p_entry & 0xFFFFE000;}

pd* gpdir = NULL;

bool alloc_page_v (page_table_entry_t* e) {
	void* p = alloc_block_p ();
	if (p==0) return false;
	
	if (get_bit_pdir(dpresent, &(gpdir->entry[(((get_addr_ptb(e)) >> 22) & 0x3ff)])) != 0) return false;
	
	*e = 0;
	*e |= ((uint32_t) p | tpresent);
	return true;
}

void free_page_v (page_table_entry_t* e) { 
	void* p = (void*)get_addr_ptb (e);
	if (p) free_block_p ((uint32_t)p); 
	clr_bit_ptb (tpresent, e);
}

page_table_entry_t* find_pt_entry_v (pt* p, uint32_t addr) { 
	if (p) return &p->entry[(((addr) >> 12) & 0x3ff)];
	return 0;
}

page_dir_entry_t* find_pd_entry_v (pd* p, uint32_t addr) { 
	if (p) return &p->entry[(((addr) >> 12) & 0x3ff)];
	return 0;
}

bool set_pdir (pd* pdir) {
	if (!pdir) return false;
	gpdir = pdir;
	write_cr3((uint32_t)pdir);
	if (read_cr3() == (uint32_t)pdir) return true;
	return false;
}

pd* get_pdir () {return gpdir;}

void map_page_v (void* phys, void* virt) {
	pd* pdir = gpdir;
	page_dir_entry_t* e = &pdir->entry [((((uint32_t)virt) >> 22) & 0x3ff)];
	
	if ((*e & tpresent) != tpresent) {
		//! page table not present, allocate it
		pt* table = (pt*) alloc_block_p ();
		if (!table) return;

		//! clear page table
		memset (table, 0, sizeof(pt));

		//! create a new entry
		page_dir_entry_t* entry =
		&pdir->entry [((((uint32_t)virt) >> 22) & 0x3ff)];

		//! map in the table (Can also just do *entry |= 3) to enable these bits
		set_bit_pdir (dpresent, entry);
		set_bit_pdir (dwritable, entry);
		set_addr_pdir ((uint32_t)table, entry);
	}
	
	//! get table
	pt* table = (pt*) (*e & ~0xfff);

	//! get page
	page_table_entry_t* page = &table->entry [((((uint32_t)virt) >> 12) & 0x3ff)];

	//! map it in (Can also do (*page |= 3 to enable..)
	set_addr_ptb ((uint32_t) phys, page);
	set_bit_ptb (tpresent, page);
}

pt* table_def = NULL;
pt* table_3gb = NULL;

void init_vm() {
	//! allocate default page table
	pt* table = (pt*) alloc_block_p ();
	if (!table) return;
	table_def = table;
 
	//! allocates 3gb page table
	pt* table2 = (pt*) alloc_block_p ();
	if (!table2) return;
	table_3gb = table2;

	//! clear page table
	memset((void*) table, 0, sizeof(pt));
	
	//! 1st 4mb are idenitity mapped
	for (int i=0, frame=0x0, virt=0x00000000; i<1024; i++, frame+=4096, virt+=4096) {
 		//! create a new page
		page_table_entry_t page=0;
		set_bit_ptb (tpresent, &page);
 		set_addr_ptb (frame, &page);

		//! ...and add it to the page table
		table2->entry [(((virt) >> 12) & 0x3ff)] = page;
	}
	
	//! map 1mb to 3gb (where we are at)
	for (int i=0, frame=0x100000, virt=0xc0000000; i<1024; i++, frame+=4096, virt+=4096) {

		//! create a new page
		page_table_entry_t page=0;
		set_bit_ptb (tpresent, &page);
 		set_addr_ptb (frame, &page);

		//! ...and add it to the page table
		table->entry [(((virt) >> 12) & 0x3ff)] = page;
	}
	
	//! create default directory table
	pd* dir = (pd*) alloc_blocks_p (3);
	if (!dir) return;
 
	//! clear directory table and set it as current
	memset (dir, 0, sizeof (pd));
	
	page_dir_entry_t* entry = &dir->entry [((0xC0000000) >> 22) & 0x3ff];
	set_bit_pdir (dpresent, entry);
	set_bit_pdir (dwritable, entry);
	set_addr_pdir ((uint32_t)table, entry);

	page_dir_entry_t* entry2 = &dir->entry [((0x00000000) >> 22) & 0x3ff];
	set_bit_pdir (dpresent, entry2);
	set_bit_pdir (dwritable, entry2);
	set_addr_pdir ((uint32_t)table2, entry2);
 
	//! switch to our page directory
	set_pdir(dir);
 
	//! enable paging	
	__asm__("mov %cr4, %eax\n\t"
			"or $0x80000001, %eax\n\t"
			"mov %eax, %cr4\n\t");
			
	//! install page fault handler
	irq_install_handler(14, pf_handler);
}

void pf_handler(struct regs* r) {
	clr();
	__asm__("cli");
	tgoto(4, 4);
	printf("%C[12]Oops!%C[7]\n\n    A fatal error occured at %i ticks. System halted.\n    Details:\n\n    %C[8]#PF\n    Code: %i (", get_ticks(), r->err_code);
	switch (r->err_code) {
		case 0b00000: printf("supervisor read a present page"); break;
		case 0b00001: printf("supervisor read a page"); break;
		case 0b00010: printf("supervisor wrote to a present page"); break;
		case 0b00011: printf("supervisor wrote to a page"); break;
		case 0b00100: printf("user read a present page"); break;
		case 0b00101: printf("user read a page"); break;
		case 0b00110: printf("user wrote to a present page"); break;
		case 0b00111: printf("user wrote to a page"); break;
		case 0b01000: printf("supervisor read a present page, reserved bits were overwritten"); break;
		case 0b01001: printf("supervisor read a page, reserved bits were overwritten"); break;
		case 0b01010: printf("supervisor overwrote reserved bits in a present page"); break;
		case 0b01011: printf("supervisor overwrote reseved bits"); break;
		case 0b01100: printf("user"); break;
		case 0b01101: printf("user"); break;
		case 0b01110: printf("user"); break;
		case 0b01111: printf("user"); break;
		case 0b10000: printf("supervisor"); break;
		case 0b10001: printf("supervisor"); break;
		case 0b10010: printf("supervisor"); break;
		case 0b10011: printf("supervisor"); break;
		case 0b10100: printf("user"); break;
		case 0b10101: printf("user"); break;
		case 0b10110: printf("user"); break;
		case 0b10111: printf("user"); break;
		case 0b11000: printf("supervisor"); break;
		case 0b11001: printf("supervisor"); break;
		case 0b11010: printf("supervisor"); break;
		case 0b11011: printf("supervisor"); break;
		case 0b11100: printf("user"); break;
		case 0b11101: printf("user"); break;
		case 0b11110: printf("user"); break;
		case 0b11111: printf("user"); break;
		default:
			printf("unknown");
	}
}

/*-----------------------------------------------------------------------*/

void* alloc_block_v () {
	for (int i = 0; i < 1024; i++) {	//go through available space in pdir
		if (get_bit_pdir(dpresent, &(gpdir->entry[i])) == 0) continue;
		if ((gpdir->entry[i]) & 0xFFC00000 == 0) continue;
		pt* tbl = (pt*) (gpdir->entry[i] & 0xFFC00000);	//point to ptable from entry number
		for (int j = 0; j < 1024; j++) {				//sift through table
			if (get_bit_ptb(tpresent, &(tbl->entry[j])) != 0 && alloc_page_v(&(tbl->entry[j]))) {	//if page is not present and page can be alloc.
				tbl->entry[j] |= (tpresent | twritable);
				printf("\n%C[3][%i] %C[7]Allocated virtual address 0x%x (i=%i, j=%i, pt=0x%x, e=%x)", get_ticks(), get_addr_ptb(&(tbl->entry[j])), i, j, (uint32_t) tbl, tbl->entry[j]);
				if (get_addr_ptb(&(tbl->entry[j])) == 0) printf (" Oops.");
				return (void*) get_addr_ptb(&(tbl->entry[j]));		//return its address
			}
		}
	}
	//no free present tables
	for (int i = 0; i < 1024; i++) {	//go through available space in pdir again
		if (gpdir->entry[i] & 1 == 1) continue;
		pt* tbl = (pt*) (gpdir->entry[i] & 0xFFC00000);	//point to ptable from entry number
		/*if (tbl) {				//if table is free...
			gpdir->entry[i] |= (1 | ((uint32_t) alloc_block_p()) & 0xFFC00000);		//make table present and alloc space
			memset((void*) (gpdir->entry[i] & 0xFFC00000), 0, sizeof(pt));			//empty it out
			alloc_page_v(&tbl->entry[0]);							//allocate the page
			printf("\n%C[3][%i] %C[7]Allocated address 0x%x", get_ticks(), (tbl->entry[0] & 0xFFFFE000));
			return (void*) (tbl->entry[0] & 0xFFFFE000);
		}*/
		for (int j = 0; j < 1024; j++) {				//sift through table
			if (get_bit_ptb(tpresent, &(tbl->entry[j])) != 0 && alloc_page_v(&(tbl->entry[j]))) {	//if page is not present and page can be alloc.
				tbl->entry[j] |= (tpresent | twritable);
				printf("\n%C[3][%i] %C[7]Allocated virtual address 0x%x (i=%i, j=%i, pt=0x%x, e=%x)", get_ticks(), get_addr_ptb(&(tbl->entry[j])), i, j, (uint32_t) tbl, tbl->entry[j]);
				if (get_addr_ptb(&(tbl->entry[j])) == 0) printf (" Oops.");
				return (void*) get_addr_ptb(&(tbl->entry[j]));		//return its address
			}
		}
	}
	printf("%C[3][%i] %C[4]Error: %C[7]No virtual memory left", get_ticks());
	//while(1);
	return NULL;
}

bool can_alloc_block_v (uint32_t addr) {
	pt* tblloc = (pt*) get_addr_pdir(&gpdir->entry[((addr)>>22)&0x3ff]);
	if (get_bit_ptb(tpresent, &(tblloc->entry[((addr)>>12)&0x3ff]))) return false;
	
	return true;
}

bool can_alloc_blocks_v (uint32_t base, uint32_t lenb) {
	for (int i = base; i < base + (lenb*4096); i+=4096)
		if (!can_alloc_block_v(base+i)) return false;
	return true;
}

void* first_free_v () {
	for (int i = 0; i < getMemSize(); i+=4096) {
		if (can_alloc_block_v(i)) return (void*) i;
	}
	return NULL;
}

void* first_free_r_v (size_t lenb) {
	for (int i = 0; i < getMemSize()-(lenb*4096); i+=4096) {
		bool isAlloc = true;
		for (int j = lenb-1; j >= 0; j--) {
			if (!can_alloc_block_v(i+(j*4096))) isAlloc = false;
		}
		if (isAlloc) return (void*) i;
	}
	return NULL;
}

void free_block_v (void* addr) {
	pt* tbl = (pt*) get_addr_pdir(&gpdir->entry[(((uint32_t)addr)>>22)&0x3ff]);
	page_table_entry_t* entry = &tbl->entry[(((uint32_t)addr)>>12)&0x3ff];
	free_page_v(entry);
	memset(addr, 0, 4096);
	printf("\n%C[3][%i] %C[7]Freed address 0x%x", get_ticks(), (int)addr);
}

void* alloc_blocks_v (size_t size) {
	void* free_b = first_free_r_v(size);
	for (int i = 0; i < size; i++) {
		pt* tbl = (pt*) get_addr_pdir(&gpdir->entry[(((uint32_t)free_b + (i*4096))>>22)&0x3ff]);
		page_table_entry_t* entry = &tbl->entry[(((uint32_t)free_b + (i*4096))>>22)&0x3ff];
		alloc_page_v(entry);
	}
	return free_b;
}

void free_blocks_v (void* addr, uint32_t len) {
	for (int i = 0; i < len; i++) {
		free_block_v((void*)((uint32_t)addr+(len*4096)));
	}
}
