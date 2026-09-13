#ifndef DUSK_SWITCH_SYS_MMAN_H
#define DUSK_SWITCH_SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED ((void*)-1)
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS

void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off);
int munmap(void* addr, size_t len);
int mprotect(void* addr, size_t len, int prot);

#endif
