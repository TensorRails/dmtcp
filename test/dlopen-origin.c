/* libdlopen-origin-a.so loads its sibling as "$ORIGIN/libdlopen-origin-b.so", as a
 * library that ships its parts beside itself does. glibc expands $ORIGIN for the
 * object that called dlopen, which DMTCP's dlopen wrapper must keep.
 * To compile, use -DLIB_A or -DLIB_B for the libraries, neither for the executable.
 */

#if defined(LIB_A)
#include <dlfcn.h>
#include <stdio.h>

int
load_b(void)
{
  void *handle = dlopen("$ORIGIN/libdlopen-origin-b.so", RTLD_NOW);
  if (handle == NULL) {
    fprintf(stderr, "dlopen $ORIGIN/libdlopen-origin-b.so failed: %s\n",
            dlerror());
    return -1;
  }
  int (*value)(void) = (int (*)(void))dlsym(handle, "value");
  int result = value != NULL ? value() : -1;
  dlclose(handle);
  return result;
}

#elif defined(LIB_B)
int
value(void)
{
  return 42;
}

#else
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
  // chdir to DMTCP root dir
  char *char1 = strrchr(argv[0], '/');
  *char1 = '\0';
  char *char2 = strrchr(argv[0], '/');
  *char2 = '\0';
  int rc = chdir(argv[0]);
  if (rc != 0) {fprintf(stderr, "Failed to chdir to %s\n", argv[0]); exit(1);}
  *char1 = *char2 = '/';

  void *a = dlopen("test/libdlopen-origin-a.so", RTLD_NOW);
  if (a == NULL) {
    fprintf(stderr, "dlopen failed: %s\n", dlerror());
    exit(1);
  }
  int (*load_b)(void) = (int (*)(void))dlsym(a, "load_b");
  if (load_b == NULL) {
    fprintf(stderr, "dlsym load_b failed: %s\n", dlerror());
    exit(1);
  }

  int cnt = 0;
  printf("0: "); fflush(stdout);
  while (1) {
    if (load_b() != 42) {
      exit(1);
    }
    if (++cnt % 1000 == 0) {
      printf(".");
      if (cnt % 50000 == 0) {
        printf("\n%d: ", cnt / 50000);
      }
      fflush(stdout);
    }
  }
  return 0;
}
#endif
