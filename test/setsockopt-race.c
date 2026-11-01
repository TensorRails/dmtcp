/* One thread sets a socket option while another closes the descriptor past
 * DMTCP's close wrapper (a raw syscall, as cuInterpose's Rust code closes
 * its sockets) and creates a socket at the same number, so that DMTCP drops
 * the old connection. setsockopt recorded the option on the connection it
 * had looked up, which the other thread could free meanwhile: the process
 * crashed in std::map, as a vLLM worker did right after a restart.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

static volatile int fd = -1;

static void *
churn(void *arg)
{
  (void)arg;
  while (1) {
    syscall(SYS_close, fd);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
  }
  return NULL;
}

int
main()
{
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  pthread_t t;
  if (pthread_create(&t, NULL, churn, NULL) != 0) {
    perror("pthread_create");
    return 1;
  }
  struct timeval tv = { 1, 0 };
  unsigned long n = 0;
  while (1) {
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (++n % 1000000 == 0) {
      printf("%lu ", n / 1000000);
      fflush(stdout);
    }
  }
}
