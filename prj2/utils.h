#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

int is_stdin_redirected();
void setnonblocking(int sock);
char **read_urls_from_file(size_t *url_count);
void free_urls(char **urls, size_t url_count);