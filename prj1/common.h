#define DEBUG 1
#define debug_print(fmt, ...)                     \
  do {                                            \
    if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); \
  } while (0)

#define IN_BUFSIZE 1024
#define MAX_MSG_SIZE 10000008
#define HEADER_SIZE 8
#define MAX_STRING_SIZE 10000000