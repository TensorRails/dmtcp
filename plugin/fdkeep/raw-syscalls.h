/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FDKEEP_RAW_SYSCALLS_H
#define FDKEEP_RAW_SYSCALLS_H

/* Bypass libc/DMTCP wrappers; return the kernel's negative errno directly. */
static inline long fdkeep_raw4(long nr, long a, long b, long c, long d)
{
#if defined(__x86_64__)
  long ret;
  register long r10 __asm__("r10") = d;
  __asm__ volatile("syscall" : "=a"(ret)
      : "0"(nr), "D"(a), "S"(b), "d"(c), "r"(r10)
      : "rcx", "r11", "memory");
  return ret;
#elif defined(__aarch64__)
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  __asm__ volatile("svc 0" : "+r"(x0)
      : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory", "cc");
  return x0;
#else
#error "fdkeep supports Linux x86-64 and ARM64"
#endif
}
#endif
