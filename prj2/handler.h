#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>

#include "common.h"
#include "utils.h"

#define DELTA 1024
#define CHUNK_SIZE 4096

enum state {
  REQUEST_NOT_RECEIVED = 0x001,
#define REQUEST_NOT_RECEIVED REQUEST_NOT_RECEIVED
  REQUEST_RECEIVED = 0x002,
#define REQUEST_RECEIVED REQUEST_RECEIVED
  REQUEST_SENT = 0x004,
#define REQUEST_SENT REQUEST_SENT
  RESPONSE_RECEIVED = 0x008,
#define RESPONSE_HEADER_RECEIVED RESPONSE_HEADER_RECEIVED
  RESPONSE_HEADER_RECEIVED = 0x010,
#define RESPONSE_RECEIVED RESPONSE_RECEIVED
  RESPONSE_SENT = 0x020,
#define RESPONSE_SENT RESPONSE_SENT
  CLIENT_OPEN = 0x100,
#define CLIENT_OPEN CLIENT_OPEN
  SERVER_OPEN = 0x200,
#define SERVER_OPEN SERVER_OPEN
};

enum content_type {
  NONE = 0,
#define NONE NONE
  CONTENT_LENGTH = 1,
#define CONTENT_LENGTH CONTENT_LENGTH
  CHUNKED = 2,
#define CHUNKED CHUNKED
};

typedef struct {
  int client_fd;
  int server_fd;
  int state;
  int content_type;
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

void reset_proxy_data(proxy_data_t *data);
void cleanup_and_close(proxy_data_t *data, int epoll_fd);
int handle_client(proxy_data_t *data, struct epoll_event *event, int epoll_fd);
int handle_server(proxy_data_t *data, struct epoll_event *event, int epoll_fd);
void parse_request(proxy_data_t *data);
int parse_response_header(proxy_data_t *data);