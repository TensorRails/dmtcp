// fork() and exec() with pthread_atfork handlers that synchronize with other
// threads, as real libraries do:
//
//  - OpenBLAS registers a prepare handler that stops its worker threads and
//    pthread_join()s them. A joined thread leaves through DMTCP's thread exit
//    path, which takes the wrapper execution lock.
//  - Handlers may also create threads (tclsh does so in its child handler,
//    see pthread_atfork2.c).
//
// DMTCP used to take the wrapper execution lock in exclusive mode before
// calling libc fork(), so all of these handlers ran under it. The OpenBLAS
// pattern then deadlocked, and a thread created from a prepare or parent
// handler corrupted the lock's reader count (rwlock.cpp assertion).
//
// The test does a burst of forks at startup, then keeps running so that the
// harness can checkpoint and restart it.

#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NUM_WORKERS 4
#define NUM_FORKS 60

static pthread_t workers[NUM_WORKERS];
static int numWorkers = 0;
static volatile int workersQuit = 0;

static void
die(const char *msg)
{
  fprintf(stderr, "fork-atfork-threads: %s\n", msg);
  _exit(1);
}

static void
touchFile(void)
{
  int fd = open("/dev/null", O_RDONLY);
  if (fd >= 0) {
    close(fd);
  }
}

static void *
workerLoop(void *arg)
{
  while (!workersQuit) {
    touchFile();
    usleep(100);
  }
  return arg;
}

static void
startWorkers(void)
{
  workersQuit = 0;
  for (int i = 0; i < NUM_WORKERS; i++) {
    if (pthread_create(&workers[i], NULL, workerLoop, NULL) != 0) {
      die("pthread_create(worker) failed");
    }
  }
  numWorkers = NUM_WORKERS;
}

// OpenBLAS-style shutdown: stop the workers and join them.
static void
stopWorkers(void)
{
  workersQuit = 1;
  for (int i = 0; i < numWorkers; i++) {
    if (pthread_join(workers[i], NULL) != 0) {
      die("pthread_join(worker) failed");
    }
  }
  numWorkers = 0;
}

static void *
shortLived(void *arg)
{
  touchFile();
  return arg;
}

static void
runShortLivedThread(void)
{
  pthread_t thread;
  if (pthread_create(&thread, NULL, shortLived, NULL) != 0) {
    die("pthread_create(short-lived) failed");
  }
  if (pthread_join(thread, NULL) != 0) {
    die("pthread_join(short-lived) failed");
  }
}

static void
prepareHandler(void)
{
  stopWorkers();
  runShortLivedThread();
  touchFile();
}

static void
parentHandler(void)
{
  runShortLivedThread();
  touchFile();
}

static void
childHandler(void)
{
  runShortLivedThread();
  touchFile();
}

static void
forkOnce(const char *self, int doExec)
{
  pid_t pid = fork();
  if (pid == -1) {
    die("fork failed");
  }
  if (pid == 0) {
    if (doExec) {
      execl(self, self, "child", (char *)NULL);
      _exit(3);
    }
    _exit(0);
  }

  int status;
  if (waitpid(pid, &status, 0) != pid) {
    die("waitpid failed");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    die("child failed");
  }
}

int
main(int argc, char **argv)
{
  if (argc > 1 && strcmp(argv[1], "child") == 0) {
    return 0;
  }

  if (pthread_atfork(prepareHandler, parentHandler, childHandler) != 0) {
    die("pthread_atfork failed");
  }

  for (int i = 0; i < NUM_FORKS; i++) {
    startWorkers();
    forkOnce(argv[0], i % 2);
  }
  printf("fork-atfork-threads: %d forks done\n", NUM_FORKS);
  fflush(stdout);

  startWorkers();
  while (1) {
    usleep(100 * 1000);
  }

  return 0;
}
