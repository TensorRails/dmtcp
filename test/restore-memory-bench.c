/* Large anonymous-memory restart benchmark. Not a GPU performance claim.
 * gcc -O2 -Iinclude test/restore-memory-bench.c -o test/restore-memory-bench
 * Launch under dmtcp_launch --no-gzip; it checkpoints once and exits.
 * On restart it reports resumption, verifies EVERY word, and exits again.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "dmtcp.h"
#pragma weak dmtcp_get_restart_env
#pragma weak dmtcp_set_ckpt_dir

static double now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static uint64_t word(size_t i)
{
  return (i * UINT64_C(0x9e3779b97f4a7c15)) ^ UINT64_C(0xa5725a2985c912e7);
}

int main(int argc, char **argv)
{
  size_t mib = argc > 1 ? strtoull(argv[1], NULL, 10) : 256;
  int sparse = argc > 2 && strcmp(argv[2], "sparse") == 0;
  /* zeroed: the odd stripes are written, then cleared, so they are resident
   * pages of zeros rather than holes. */
  int zeroed = argc > 2 && strcmp(argv[2], "zeroed") == 0;
  int cow = argc > 3 && strcmp(argv[3], "cow") == 0;
  const size_t stripeWords = 64 * 1024 * 1024 / 8;
  if (!mib || mib > 65536) return 2;
  size_t bytes = mib * 1024 * 1024;
  /* Start on a 2 MiB boundary, so each zeroed stripe is whole MiBs for the
   * checkpoint writer, whatever mapping lies next to this one. */
  const size_t align = 2 * 1024 * 1024;
  char *raw = mmap(NULL, bytes + align, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (raw == MAP_FAILED) { perror("mmap"); return 2; }
  uint64_t *p = (uint64_t *) (((uintptr_t) raw + align - 1) & ~(align - 1));
  if ((char *) p > raw) munmap(raw, (char *) p - raw);
  munmap((char *) p + bytes, raw + align - (char *) p);
  /* Match the baseline's ordinary anonymous pages at checkpoint creation. */
  if (madvise(p, bytes, MADV_NOHUGEPAGE)) { perror("madvise"); return 2; }
  for (size_t i = 0; i < bytes / 8; ++i) {
    if (!sparse || (i / stripeWords) % 2 == 0) p[i] = word(i);
  }
  for (size_t i = stripeWords; zeroed && i < bytes / 8; i += 2 * stripeWords) {
    size_t n = bytes / 8 - i < stripeWords ? bytes / 8 - i : stripeWords;
    memset(p + i, 0, n * 8);
  }
  printf("BENCH_CHECKPOINT mib=%zu sparse=%d zeroed=%d\n", mib, sparse, zeroed);
  fflush(stdout);
  int result = dmtcp_checkpoint();
  if (result == DMTCP_NOT_PRESENT) return 3;
  if (result == DMTCP_AFTER_CHECKPOINT) return 0;
  if (result != DMTCP_AFTER_RESTART) return 4;
  char receiver[32];
  if (!dmtcp_get_restart_env ||
      dmtcp_get_restart_env("DMTCP_BENCH_RESTART_ENV", receiver,
                            sizeof(receiver)) != RESTART_ENV_SUCCESS ||
      strcmp(receiver, "receiver") != 0) {
    fprintf(stderr, "BENCH_RESTART_ENV_FAILED\n");
    return 6;
  }
  double start = now();
  struct rusage before, after;
  getrusage(RUSAGE_SELF, &before);
  printf("BENCH_RESUMED monotonic=%.6f mib=%zu\n", start, mib);
  fflush(stdout);
  for (size_t i = 0; i < bytes / 8; ++i) {
    uint64_t expected = (sparse || zeroed) && (i / stripeWords) % 2 ? 0 : word(i);
    if (p[i] != expected) {
      fprintf(stderr, "BENCH_CORRUPT word=%zu got=%" PRIx64 " expected=%" PRIx64 "\n",
              i, p[i], expected);
      return 5;
    }
  }
  getrusage(RUSAGE_SELF, &after);
  printf("BENCH_VERIFIED seconds=%.6f minflt=%ld majflt=%ld\n", now() - start,
         after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt);
  FILE *f = fopen("/proc/self/smaps_rollup", "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      if (strstr(line, "AnonHugePages:") || strstr(line, "Rss:")) fputs(line, stdout);
    }
    fclose(f);
  }
  fflush(stdout);
  if (cow) {
    /* Dirty one word per page, checkpoint again, then verify the dirty AND
     * untouched words after another restore. The first image must stay intact. */
    for (size_t i = 0; i < bytes / 8; i += 4096 / 8) p[i] ^= UINT64_C(0xfedcba9876543210);
    char second[4096];
    if (!dmtcp_set_ckpt_dir ||
        dmtcp_get_restart_env("DMTCP_BENCH_SECOND_DIR", second, sizeof(second)) != RESTART_ENV_SUCCESS)
      return 7;
    dmtcp_set_ckpt_dir(second);
    int secondResult = dmtcp_checkpoint();
    if (secondResult == DMTCP_AFTER_CHECKPOINT) return 0;
    if (secondResult != DMTCP_AFTER_RESTART) return 8;
    for (size_t i = 0; i < bytes / 8; ++i) {
      uint64_t expected = (sparse || zeroed) && (i / stripeWords) % 2 ? 0 : word(i);
      if (i % (4096 / 8) == 0) expected ^= UINT64_C(0xfedcba9876543210);
      if (p[i] != expected) return 9;
    }
    puts("BENCH_SECOND_VERIFIED");
    fflush(stdout);
  }
  munmap(p, bytes);
  return 0;
}
