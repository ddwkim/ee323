// Step 1-1. Starting Your Proxy: Listen for Incoming Connections
// When your proxy starts, establish a socket connection to listen for incoming
// connections. Your proxy should listen on the port specified from the command
// line, and wait for incoming client connections. Once a client has connected,
// the proxy should read data from the client and then check for a properly
// formatted HTTP request. An invalid request from the client should be answered
// with an appropriate error code. You should return formatted error message
// (400 Bad Request) in the following cases: When a request from a client does
// NOT have a “host” header field. This is because some web servers require the
// “host” http header,  so it is better to add this header whenever making a
// request. When HTTP methods other than “GET” are used while requesting data.
// This does not mean that HTTP version 1.0 cannot use other methods. We
// simplified the assignment for your convenience. When different HTTP versions
// (other than v1.0)  are used while requesting data. When invalid “host” header
// fields are passed (hint: use gethostbyname() to check if the host name is
// invalid).

// Step 1-2. Parse the URL
// Once the proxy sees a valid HTTP request, it will need to parse the requested
// URL. The proxy needs at most three pieces of information: (i) the requested
// host, (ii) the requested port, and (iii) the requested path. Following
// functions would be helpful for this task.

// Step 2. Get Data from the Remote Server
// Once the proxy has parsed the URL, it can make a connection to the requested
// host. When making a connection, use the appropriate remote port, or the
// default of 80 if none is specified. The proxy then sends the HTTP request
// that it received from the client to the remote server.

// Step 3. Transfer Response of the Server to the Client
// After the response from the remote server is received, the proxy should send
// the response message to the client via the appropriate socket. Close the
// connection once the transaction is complete.

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "handler.h"
#include "utils.h"

#define MAX_EVENTS 100
#define BACKLOG 1024

int main(int argc, char *argv[]) {
  int port;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  port = atoi(argv[1]);

  if (port <= 0 || port > 65535) {
    fprintf(stderr, "Invalid port number");
    exit(EXIT_FAILURE);
  }

  size_t black_url_count = 0;
  char **black_urls = NULL;

  if (is_stdin_redirected()) {
    black_urls = read_urls_from_file(&black_url_count);
    if (!black_urls) {
      fprintf(stderr, "Failed to read urls from file");
      exit(EXIT_FAILURE);
    }
  }

  int sockfd;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  int yes = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    perror("setsockopt");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  saddr.sin_port = htons(port);
  if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  if (listen(sockfd, BACKLOG) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  struct epoll_event ev, events[MAX_EVENTS];
  ev.events = EPOLLIN;
  ev.data.fd = sockfd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
    perror("epoll_ctl listen_sock");
    exit(EXIT_FAILURE);
  }

  while (1) {
    int n = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == sockfd) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd =
            accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          } else {
            perror("accept");
            break;
          }
        }
        setnonblocking(client_fd);

        ev.data.fd = client_fd;
        ev.events = EPOLLIN | EPOLLET;

        proxy_data_t *data = calloc(1, sizeof(proxy_data_t));
        data->client_fd = client_fd;
        data->state |= CLIENT_OPEN;
        data->state |= REQUEST_NOT_RECEIVED;
        ev.data.ptr = data;

        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
          perror("epoll_ctl add client");
          close(client_fd);
          free(data);
          break;
        }
        DEBUG_PRINT("client connected: %d\n", client_fd);

      } else {
        proxy_data_t *data = (proxy_data_t *)events[i].data.ptr;
        if (data->client_fd == events[i].data.fd) {
          handle_client(data, &events[i], epollfd);
        }
        if (data->server_fd == events[i].data.fd) {
          handle_server(data, &events[i], epollfd);
        }
      }
    }
  }

  free_urls(black_urls, black_url_count);
  close(epollfd);
  close(sockfd);

  return 0;
}
