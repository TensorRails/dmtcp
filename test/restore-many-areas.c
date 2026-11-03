/* Many areas, writable ones of 1 MiB and more between small read-only ones,
 * survive a restart.
 * gcc -O2 -Iinclude test/restore-many-areas.c -o test/restore-many-areas
 * Launch under dmtcp_launch --no-gzip; it checkpoints once and exits.
 * Restart reads the writable areas' bytes in batches, across areas and in
 * shares that start and end inside them, and the read-only ones at once; on
 * restart this verifies every byte of both and each area's protection.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "dmtcp.h"

/* More writable areas than restart defers at once (RESTART_DEFERRED_MAX). */
#define AREAS 2400

/* Writable areas are at least the 1 MiB restart defers (RESTART_DEFER_MIN). */
static size_t pages(size_t i) { return (i % 2 ? 1 : 256) + (i * 7919) % 23; }

static uint64_t word(size_t area, size_t i)
{
  return ((area + 1) * UINT64_C(0x9e3779b97f4a7c15)) ^ (i * UINT64_C(0xbf58476d1ce4e5b9));
}

int main(void)
{
  size_t page = sysconf(_SC_PAGESIZE), total = 0;
  for (size_t i = 0; i < AREAS; i++) total += pages(i) * page;
  /* One mapping, then alternate protections, so no two neighbors merge. */
  char *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base == MAP_FAILED) { perror("mmap"); return 2; }
  char *p = base;
  for (size_t i = 0; i < AREAS; i++) {
    uint64_t *w = (uint64_t *) p;
    size_t n = pages(i) * page / 8;
    for (size_t j = 0; j < n; j++) w[j] = word(i, j);
    if (i % 2 && mprotect(p, pages(i) * page, PROT_READ)) { perror("mprotect"); return 2; }
    p += pages(i) * page;
  }
  printf("MANY_CHECKPOINT areas=%d bytes=%zu\n", AREAS, total);
  fflush(stdout);
  int result = dmtcp_checkpoint();
  if (result == DMTCP_NOT_PRESENT) return 3;
  if (result == DMTCP_AFTER_CHECKPOINT) return 0;
  if (result != DMTCP_AFTER_RESTART) return 4;
  p = base;
  for (size_t i = 0; i < AREAS; i++) {
    uint64_t *w = (uint64_t *) p;
    size_t n = pages(i) * page / 8;
    for (size_t j = 0; j < n; j++) {
      if (w[j] != word(i, j)) {
        fprintf(stderr, "MANY_CORRUPT area=%zu word=%zu\n", i, j);
        return 5;
      }
    }
    p += pages(i) * page;
  }
  /* Each odd area must be read-only again, and each even one writable. */
  FILE *maps = fopen("/proc/self/maps", "r");
  if (!maps) return 6;
  char line[512];
  size_t readonly = 0, writable = 0;
  while (fgets(line, sizeof(line), maps)) {
    unsigned long start, end;
    char perms[5];
    if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3) continue;
    if (start < (unsigned long) base || end > (unsigned long) (base + total)) continue;
    if (perms[1] == 'w') writable++; else readonly++;
  }
  fclose(maps);
  if (readonly != AREAS / 2 || writable != AREAS / 2) {
    fprintf(stderr, "MANY_PROT readonly=%zu writable=%zu\n", readonly, writable);
    return 7;
  }
  puts("MANY_VERIFIED");
  fflush(stdout);
  return 0;
}
