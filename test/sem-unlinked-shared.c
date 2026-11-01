/* Two processes that share named semaphores unlinked at once, as CPython's
 * multiprocessing creates its locks: each maps the deleted /dev/shm file and
 * holds no descriptor for it. A restart must recreate one file that both map
 * again; with a file each, a post would no longer reach the other process and
 * its wait would time out.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static sem_t *
open_unlinked(const char *role)
{
  char name[64];
  snprintf(name, sizeof name, "/dmtcp-sem-%s-%d", role, getpid());
  sem_t *s = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
  if (s == SEM_FAILED) {
    perror("sem_open");
    exit(1);
  }
  sem_unlink(name);
  return s;
}

static void
wait_for(sem_t *s)
{
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  t.tv_sec += 10;
  while (sem_timedwait(s, &t) != 0) {
    if (errno != EINTR) {
      perror("sem_timedwait");
      exit(1);
    }
  }
}

int
main()
{
  sem_t *ping = open_unlinked("ping");
  sem_t *pong = open_unlinked("pong");
  if (fork() == 0) {
    while (1) {
      wait_for(ping);
      sem_post(pong);
    }
  }
  int cnt = 0;
  while (1) {
    sem_post(ping);
    wait_for(pong);
    if (++cnt % 1000 == 0) {
      printf("%d ", cnt / 1000);
      fflush(stdout);
      usleep(100000);
    }
  }
}
