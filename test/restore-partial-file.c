/* Verify the private dirty tail of a regular file's final partial page.
 * gcc -O2 -Iinclude test/restore-partial-file.c -o test/restore-partial-file
 * Run under dmtcp_launch --no-gzip, then dmtcp_restart its image.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "dmtcp.h"

int main(void)
{
  char path[] = "/tmp/dmtcp-partial-file-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0 || ftruncate(fd, 3001)) return 2;
  unsigned char *p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (p == MAP_FAILED) return 3;
  close(fd);
  for (size_t i = 0; i < 4096; i++) p[i] = (i * 17 + 39) & 255;
  if (mprotect(p, 8192, PROT_READ)) return 4;
  int result = dmtcp_checkpoint();
  if (result == DMTCP_AFTER_CHECKPOINT) return 0;
  if (result != DMTCP_AFTER_RESTART) return 5;
  for (size_t i = 0; i < 4096; i++) {
    if (p[i] != ((i * 17 + 39) & 255)) {
      fprintf(stderr, "PARTIAL_FILE_CORRUPT byte=%zu\n", i);
      return 6;
    }
  }
  puts("PARTIAL_FILE_VERIFIED");
  munmap(p, 8192);
  unlink(path);
  return 0;
}
