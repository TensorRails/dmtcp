/* SPDX-License-Identifier: Apache-2.0
 * Real checkpoint/restore of descriptors created outside DMTCP wrappers. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>
#include "dmtcp.h"
#include "../plugin/fdkeep/raw-syscalls.h"

#define RAW(n,a,b,c,d) fdkeep_raw4(SYS_##n,(long)(a),(long)(b),(long)(c),(long)(d))
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FDKEEP_FAILED line=%d\n",__LINE__); return 2; } } while (0)

int main(void)
{
  int event = RAW(eventfd2, 7, EFD_CLOEXEC | EFD_NONBLOCK, 0, 0);
  int pipes[2];
  CHECK(event >= 0 && RAW(pipe2, pipes, O_CLOEXEC | O_NONBLOCK, 0, 0) == 0);
  CHECK(RAW(write, pipes[1], "fdkeep", 6, 0) == 6);
  int ep = epoll_create1(EPOLL_CLOEXEC);
  struct epoll_event watch = {.events = EPOLLIN, .data.u64 = 0xfeed1234};
  CHECK(ep >= 0 && RAW(epoll_ctl, ep, EPOLL_CTL_ADD, pipes[0], &watch) == 0);
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  char path[] = "/tmp/dmtcp-fdkeep-XXXXXX";
  int tmp = mkstemp(path);
  CHECK(tmp >= 0);
  close(tmp); unlink(path);
  strcpy(addr.sun_path, path);
  int listener = RAW(socket, AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, 0);
  CHECK(listener >= 0 && RAW(bind, listener, &addr, sizeof(addr), 0) == 0);
  CHECK(RAW(listen, listener, 1, 0, 0) == 0);
  int result = dmtcp_checkpoint();
  if (result == DMTCP_AFTER_CHECKPOINT) return 0;
  CHECK(result == DMTCP_AFTER_RESTART);
  uint64_t counter = 0;
  CHECK(RAW(read, event, &counter, sizeof(counter), 0) == sizeof(counter) && counter == 7);
  struct epoll_event ready;
  CHECK(epoll_wait(ep, &ready, 1, 1000) == 1 && ready.data.u64 == watch.data.u64);
  char data[6];
  CHECK(RAW(read, pipes[0], data, sizeof(data), 0) == sizeof(data) && !memcmp(data, "fdkeep", 6));
  int client = RAW(socket, AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, 0);
  CHECK(client >= 0 && RAW(connect, client, &addr, sizeof(addr), 0) == 0);
  unlink(path);
  puts("FDKEEP_VERIFIED");
  return 0;
}
