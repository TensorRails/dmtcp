/* A child that keeps a pipe only through an F_DUPFD_CLOEXEC duplicate, as the
 * child of CPython's multiprocessing spawn keeps its parent sentinel (os.dup).
 * With _FILE_OFFSET_BITS=64, glibc 2.28 and later compile fcntl into fcntl64,
 * which DMTCP must see: otherwise the parent's checkpoint drains its end of
 * the pipe forever.
 */
#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
  if (argc == 2) {
    // The exec'd child: argv[1] is the pipe's read end.
    int fd = atoi(argv[1]);
    int dup = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (dup < 0) {
      perror("fcntl(F_DUPFD_CLOEXEC)");
      exit(1);
    }
    close(fd);
    while (1) {
      sleep(1);
    }
  }

  int p[2];
  if (pipe(p) != 0) {
    perror("pipe");
    exit(1);
  }
  pid_t pid = fork();
  if (pid == 0) {
    close(p[1]);
    char arg[16];
    snprintf(arg, sizeof arg, "%d", p[0]);
    execl(argv[0], argv[0], arg, (char *)NULL);
    perror("execl");
    exit(1);
  }
  // The parent keeps the write end and writes nothing, as spawn's does.
  close(p[0]);
  int cnt = 0;
  printf("0: ");
  fflush(stdout);
  while (1) {
    usleep(100000);
    if (++cnt % 10 == 0) {
      printf(".");
      fflush(stdout);
    }
  }
  return 0;
}
