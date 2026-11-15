// Detached threads exit while other threads create threads and the main
// thread forks.
//
// glibc caches the stacks of exited threads. When the cache grows past its
// limit, the exiting thread frees old stacks, and their TLS, while it holds
// the stack cache lock (__nptl_deallocate_stack -> _dl_deallocate_tls ->
// free). DMTCP's free() wrapper skips the wrapper execution lock for an
// exiting thread, but it used to resolve the real free() lazily, from a
// separate cache in that branch. The first exiting thread to get there
// called dmtcp_dlsym(), which takes the wrapper execution lock:
//
//  - the exiting thread holds the stack cache lock and waits for a read lock
//    behind a queued writer (fork or checkpoint);
//  - a thread in pthread_create() holds a read lock and waits for the stack
//    cache lock;
//  - the writer waits for that read lock to be released.
//
// The window exists once per process image, at the first stack eviction, so
// the test re-executes itself ROUNDS times. It turns the stack cache off
// (GLIBC_TUNABLES), so that evictions start with the first exiting threads,
// and it forks from several threads, so that a writer is usually queued.
// The fork() calls themselves are serialized: DMTCP's rwlock does not
// support two writers queued at once (a separate issue).
// Without the fix nearly every run hangs. Then the test keeps running so that
// the harness can checkpoint and restart it.

#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NUM_SPAWNERS 4
#define NUM_FORKERS 2
#define ROUNDS 150
#define STACK_CACHE_TUNABLE "glibc.pthread.stack_cache_size=0"

static volatile int spawnDelayUs = 0;
static pthread_mutex_t forkLock = PTHREAD_MUTEX_INITIALIZER;

static void
die(const char *msg)
{
  fprintf(stderr, "pthread-exit-fork: %s\n", msg);
  _exit(1);
}

static void *
shortLived(void *arg)
{
  int fd = open("/dev/null", O_RDONLY);
  if (fd >= 0) {
    close(fd);
  }
  return arg;
}

// Keep creating detached threads with the default (large) stack size, so
// that the stack cache overflows and exiting threads free cached stacks.
static void *
spawner(void *arg)
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  while (1) {
    pthread_t thread;
    if (pthread_create(&thread, &attr, shortLived, NULL) != 0) {
      // Too many threads alive at once; let some exit.
      usleep(1000);
      continue;
    }
    if (spawnDelayUs > 0) {
      usleep(spawnDelayUs);
    }
  }
  return arg;
}

static void
startSpawners(void)
{
  for (int i = 0; i < NUM_SPAWNERS; i++) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, spawner, NULL) != 0) {
      die("pthread_create(spawner) failed");
    }
  }
}

static void
forkOnce(void)
{
  pthread_mutex_lock(&forkLock);
  pid_t pid = fork();
  if (pid == 0) {
    _exit(0);
  }
  pthread_mutex_unlock(&forkLock);
  if (pid == -1) {
    die("fork failed");
  }

  int status;
  if (waitpid(pid, &status, 0) != pid) {
    die("waitpid failed");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    die("child failed");
  }
}

static void *
forker(void *arg)
{
  while (1) {
    forkOnce();
  }
  return arg;
}

static void
execRound(const char *self, int round)
{
  char next[16];
  snprintf(next, sizeof(next), "%d", round);
  execl(self, self, next, (char *)NULL);
  die("execl failed");
}

int
main(int argc, char **argv)
{
  int round = argc > 1 ? atoi(argv[1]) : 0;

  // glibc reads tunables at startup, so set it and start over.
  const char *tunables = getenv("GLIBC_TUNABLES");
  if (round == 0 && (tunables == NULL || strstr(tunables, "stack_cache_size")
                     == NULL)) {
    if (setenv("GLIBC_TUNABLES", STACK_CACHE_TUNABLE, 1) != 0) {
      die("setenv failed");
    }
    execRound(argv[0], 1);
  }

  startSpawners();

  if (round < ROUNDS) {
    for (int i = 0; i < NUM_FORKERS; i++) {
      pthread_t thread;
      if (pthread_create(&thread, NULL, forker, NULL) != 0) {
        die("pthread_create(forker) failed");
      }
    }
    forkOnce();
    execRound(argv[0], round + 1);
  }

  printf("pthread-exit-fork: %d rounds done\n", ROUNDS);
  fflush(stdout);

  // Steady state for checkpoint and restart: slower thread churn, no forks,
  // so that the harness sees a single process.
  spawnDelayUs = 1000;
  while (1) {
    usleep(100 * 1000);
  }

  return 0;
}
