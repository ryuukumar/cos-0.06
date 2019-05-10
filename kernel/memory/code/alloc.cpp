#include <stddef.h>
#include <stdint.h>

#include <mmain.h>
#include <mpmng.h>
#include <mvmng.h>
#include <sys/memory.h>

#define ENTRIES_PER_STACK

m_block* stacks [6] = {0};
uint8_t* data [12] = {0};

void init_mem() {
	init_pm();
	init_vm();
	
	for (int i = 0; i < 6; i++) {stacks[i] = (m_block*) alloc_block_v(); memset(stacks[i], 0, 4096);}
	for (int i = 0; i < 12; i++) {data[i] = (uint8_t*) alloc_block_v(); memset(data[i], 0, 4096);}
}

bool isOccupied(void* ptr) {
	for(int i = 0; i < (6*341); i++) {
		m_block* ptr2 = stacks[i];
		ptr2+=i%6;
		if (ptr2->base + ptr2->len != 0 && ptr2->base < (uint32_t)ptr && ptr2->base + ptr2->len > (uint32_t)ptr && ptr2->occupied) return true;
	}
	return false;
}

void* first_free_data (size_t size) {
	for (int i = 0; i < (12*4096)-size; i++) {
		bool isAlloc;
		for (int j = size-1; j > 0; j--) {
			uint8_t* ptr = data[i];
			ptr+=j;
			if (!isOccupied(ptr)) return ptr;
		}
	}
}

void* set_alloc(m_block* target, size_t size) {
	m_block* ptr = target;
	ptr->base = (uint32_t) first_free_data(size);
	ptr->len = (uint32_t) size;
	ptr->occupied = true;
	return (void*) ptr->base;
}

void* malloc(size_t bytes) {
	for (int i=0; i<6; i++) {
		for (int j=0; j<341; j++) {
			m_block* ptr = stacks[i];
			ptr+=j;
			if (!ptr->occupied) return set_alloc(ptr, bytes);
		}
	}
	return NULL;
}

void free(void* addr) {
	for (int i=0; i<(6*341); i++) {
		m_block* ptr = stacks[i];
		ptr+=i%60;
		if (ptr->base == (uint32_t)addr) {
			memset((void*)ptr->base, 0, ptr->len);
			memset((void*)ptr, 0, sizeof(m_block));
		}
	}
}
