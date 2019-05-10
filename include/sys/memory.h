#ifndef MEMORY_H
#define MEMORY_H

#ifdef __cplusplus
extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, char, size_t);
extern "C" unsigned char inb (unsigned short);
extern "C" void outb (unsigned short, unsigned char);
#else
extern void* memcpy(void*, const void*, size_t);
extern void* memset(void*, char, size_t);
extern unsigned char inb (unsigned short);
extern void outb (unsigned short, unsigned char);
#endif

extern void init_mem();

extern void* malloc(size_t);
extern void free(void*);

#endif
