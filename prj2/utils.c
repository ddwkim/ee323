#include "utils.h"

int is_stdin_redirected() {
  struct stat stat_buf;
  fstat(fileno(stdin), &stat_buf);
  return S_ISREG(stat_buf.st_mode);
}

void setnonblocking(int sock) {
  int opts;

  opts = fcntl(sock, F_GETFL);
  if (opts < 0) {
    perror("fcntl(F_GETFL)");
    exit(1);
  }
  opts = (opts | O_NONBLOCK);
  if (fcntl(sock, F_SETFL, opts) < 0) {
    perror("fcntl(F_SETFL)");
    exit(1);
  }
}

char **read_urls_from_file(size_t *url_count) {
  size_t url_capacity = 10;
  char **urls = malloc(url_capacity * sizeof(char *));
  if (!urls) {
    perror("malloc");
    return NULL;
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  while ((read = getline(&line, &len, stdin)) != -1) {
    if (read > 0 && line[read - 1] == '\n') {
      line[read - 1] = '\0';  // Remove newline character
    }

    if (*url_count == url_capacity) {
      url_capacity *= 2;
      char **tmp = realloc(urls, url_capacity * sizeof(char *));
      if (!tmp) {
        perror("realloc");
        free(line);
        for (size_t i = 0; i < *url_count; i++) {
          free(urls[i]);
        }
        free(urls);
        return NULL;
      }
      urls = tmp;
    }

    urls[*url_count] = strdup(line);
    (*url_count)++;
  }

  free(line);

  return urls;
}

void free_urls(char **urls, size_t url_count) {
  for (size_t i = 0; i < url_count; i++) {
    free(urls[i]);
  }
  free(urls);
}
