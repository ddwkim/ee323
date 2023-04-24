#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>

#include "common.h"
#include "utils.h"

#define DELTA 1024
#define CHUNK_SIZE 4096 * 2

typedef enum {
  REQUEST_NOT_RECEIVED = 0x001,
  REQUEST_RECEIVED = 0x002,
  REQUEST_SENT = 0x004,
  RESPONSE_RECEIVED = 0x008,
  RESPONSE_HEADER_RECEIVED = 0x010,
  RESPONSE_SENT = 0x020,
  CLIENT_OPEN = 0x100,
  SERVER_OPEN = 0x200,
} state_t;

typedef enum {
  NONE = 0,
  CONTENT_LENGTH = 1,
  CHUNKED = 2,
} content_type_t;

typedef struct fd_data_t fd_data_t;

typedef struct {
  int client_fd;
  int server_fd;
  char **black_urls;
  int black_urls_count;
  fd_data_t *client_fd_data;
  fd_data_t *server_fd_data;
  state_t state;
  content_type_t content_type;
  struct hostent *host_entry;
  int port;
  uint32_t bytes_sent;
  uint32_t header_length;
  uint32_t content_length;
  char *req_buf;
  uint32_t req_buf_used;
  uint32_t req_buf_capacity;
  char *res_buf;
  uint32_t res_buf_used;
  uint32_t res_buf_capacity;
} proxy_data_t;

struct fd_data_t {
  int fd;
  proxy_data_t *data;
};

void reset_proxy_data(proxy_data_t *fd_data);
void cleanup_and_close(proxy_data_t *fd_data, int epoll_fd);
int handle_client(proxy_data_t *data, struct epoll_event *event, int epoll_fd);
int handle_server(proxy_data_t *data, struct epoll_event *event, int epoll_fd);
void parse_request(proxy_data_t *data);
int parse_response_header(proxy_data_t *data);
int is_blacklisted(char *host, char **black_urls, int black_urls_count);