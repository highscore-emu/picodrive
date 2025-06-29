#define _GNU_SOURCE 1 // mremap
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

/* Mostly copied from the JG port */

void *
plat_mmap (unsigned long addr, size_t size, int need_exec, int is_fixed)
{
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  void *req, *ret;

  req = (void *)(uintptr_t)addr;
  ret = mmap(req, size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (ret == MAP_FAILED)
    return NULL;

  if (is_fixed && addr != 0 && ret != (void*)(uintptr_t) addr) {
    munmap (ret, size);
    return NULL;
  }

  return ret;
}

void
plat_munmap (void *ptr, size_t size)
{
  if (ptr != NULL)
    munmap (ptr, size);
}

void *
plat_mremap (void *ptr, size_t oldsize, size_t newsize)
{
  void *tmp, *ret;
  size_t preserve_size;

  preserve_size = oldsize;
  if (preserve_size > newsize)
    preserve_size = newsize;

  tmp = malloc (preserve_size);
  if (tmp == NULL)
    return NULL;

  memcpy (tmp, ptr, preserve_size);

  munmap (ptr, oldsize);
  ret = mmap (ptr, newsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (ret == MAP_FAILED) {
    free(tmp);
    return NULL;
  }

  memcpy (ret, tmp, preserve_size);
  free (tmp);
  return ret;
}

// if NULL is returned, static buffer is used
void *
plat_mem_get_for_drc (size_t size)
{
  return NULL;
}

int
plat_mem_set_exec (void *ptr, size_t size)
{
  return mprotect (ptr, size, PROT_READ | PROT_WRITE | PROT_EXEC);
}
