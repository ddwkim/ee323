#include <stdio.h>
#include <unistd.h>

#define DEBUG 1
#define DEBUG_PRINT(fmt, ...)                     \
  do {                                            \
    if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); \
  } while (0)
