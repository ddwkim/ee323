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
    DEBUG_PRINT("black_url_count: %zu\n", black_url_count);
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

        ev.events = EPOLLIN | EPOLLET;

        proxy_data_t *data = calloc(1, sizeof(proxy_data_t));
        fd_data_t *client_fd_data = calloc(1, sizeof(fd_data_t));
        client_fd_data->fd = client_fd;
        client_fd_data->data = data;
        data->client_fd = client_fd;
        data->client_fd_data = client_fd_data;
        data->server_fd = -1;
        data->state |= CLIENT_OPEN;
        data->state |= REQUEST_NOT_RECEIVED;
        data->black_urls = black_urls;
        data->black_urls_count = black_url_count;

        ev.data.ptr = client_fd_data;

        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
          perror("epoll_ctl add client");
          close(client_fd);
          free(data);
          free(client_fd_data);
          break;
        } else {
          DEBUG_PRINT("client connected: %d\n", client_fd);
        };

      } else {
        fd_data_t *fd_data = (fd_data_t *)events[i].data.ptr;
        if (fd_data->data->client_fd == fd_data->fd) {
          handle_client(fd_data->data, &events[i], epollfd);
        }
        if (fd_data->data->server_fd == fd_data->fd) {
          handle_server(fd_data->data, &events[i], epollfd);
        }
      }
    }
  }

  free_urls(black_urls, black_url_count);
  close(epollfd);
  close(sockfd);

  return 0;
}
