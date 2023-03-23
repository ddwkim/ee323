#define DEBUG 0
#define DEBUG_PRINT(fmt, ...)                     \
  do {                                            \
    if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); \
  } while (0)

#define IN_BUFSIZE 1024
#define MAX_MSG_SIZE 10000000
#define HEADER_SIZE 8
#define MAX_STRING_SIZE (MAX_MSG_SIZE - HEADER_SIZE)