#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

#define MAX_EVENTS 50
#define BACKLOG 1024

typedef struct {
  int client_fd;
  uint16_t op;
  uint16_t shift;
  uint32_t msg_size;
  uint32_t bytes_recv;
  uint32_t bytes_sent;
  uint16_t processed;
  char *msg;
} ConnectionInfo;

void caesar_cipher(char *buffer, uint32_t len, uint16_t shift, uint16_t op) {
  if (op == 1) {
    shift = 26 - shift;
  }

  for (int i = HEADER_SIZE; i < len; i++) {
    if (buffer[i] >= 'A' && buffer[i] <= 'Z') {
      buffer[i] = ((buffer[i] - 'A' + shift) % 26) + 'a';
    } else if (buffer[i] >= 'a' && buffer[i] <= 'z') {
      buffer[i] = ((buffer[i] - 'a' + shift) % 26) + 'a';
    }
  }
}

void reset_client_data(ConnectionInfo *client_data) {
  client_data->op = 0;
  client_data->shift = 0;
  client_data->msg_size = 0;
  client_data->bytes_recv = 0;
  client_data->bytes_sent = 0;
  client_data->processed = 0;
}

int handle_client(ConnectionInfo *client_data, struct epoll_event *event,
                  int epoll_fd) {
  int fd = client_data->client_fd;
  char *msg = client_data->msg;
  uint32_t *msg_size = &(client_data->msg_size);
  uint32_t *bytes_recv = &(client_data->bytes_recv);
  uint32_t *bytes_sent = &(client_data->bytes_sent);

  while (1) {
    // Keep reading until we have a full header

    if (*bytes_recv < HEADER_SIZE) {
      ssize_t count = recv(fd, msg + *bytes_recv, HEADER_SIZE - *bytes_recv, 0);

      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("recv header");
          reset_client_data(client_data);
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
          close(fd);
          return -1;
        }
      } else if (count == 0) {
        reset_client_data(client_data);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
      }

      *bytes_recv += count;

      // Fill metadata
      if (*bytes_recv == HEADER_SIZE) {
        client_data->op = ntohs(*((uint16_t *)msg));
        client_data->shift = ntohs(*((uint16_t *)(msg + 2)));
        *msg_size = ntohl(*((uint32_t *)(msg + 4)));
        debug_print("op : %d, shift : %d, msg_size : %d\n", client_data->op,
                    client_data->shift, *msg_size);

        if (*msg_size > MAX_MSG_SIZE) {
          debug_print(
              "Message too large, should be less than 10MB : received %d\n",
              *msg_size);
          reset_client_data(client_data);
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
          close(fd);
          return -1;
        }

        if (client_data->op != 0 && client_data->op != 1) {
          debug_print("Invalid operation, should be 0 or 1 : received %d\n",
                      client_data->op);
          reset_client_data(client_data);
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
          close(fd);
          return -1;
        }
      }
    }

    // Header received, Keep reading until we have a full message
    if (*msg_size > 0 && *bytes_recv < *msg_size) {
      ssize_t count = recv(fd, msg + *bytes_recv, *msg_size - *bytes_recv, 0);
      debug_print("bytes_recv : % d\n", *bytes_recv);

      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("recv content");
          reset_client_data(client_data);
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
          close(fd);
          return -1;
        }
      } else if (count == 0) {
        reset_client_data(client_data);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
      }

      *bytes_recv += count;
    }

    // Process the message
    else if (*msg_size > 0 && *bytes_recv == *msg_size &&
             client_data->processed == 0) {
      debug_print("Processing message of size %d\n", *msg_size);
      caesar_cipher(msg, *msg_size, client_data->shift, client_data->op);
      client_data->processed = 1;
      // msg[*msg_size] = '\0';
      // fprintf(stderr, "%s", (msg + HEADER_SIZE));

      *bytes_sent = 0;

      // Modify the event to monitor for output readiness
      event->events = EPOLLOUT | EPOLLET;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, event) == -1) {
        perror("epoll_ctl change mode to out");
        reset_client_data(client_data);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
      }
    }

    if (client_data->processed == 1 && *bytes_sent < *msg_size) {
      ssize_t count =
          send(fd, client_data->msg + *bytes_sent, *msg_size - *bytes_sent, 0);
      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("send");
          reset_client_data(client_data);
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
          close(fd);
          return -1;
        }
      } else if (count == 0) {
        reset_client_data(client_data);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
      }

      *bytes_sent += count;
      debug_print("bytes_sent : %d\n", *bytes_sent);
    }

    if (*bytes_sent == *msg_size) {
      reset_client_data(client_data);
      // sending finished, modify the event to monitor for input readiness
      event->events = EPOLLIN | EPOLLET;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, event) == -1) {
        perror("epoll_ctl : change mode to in");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
      }
      break;
    }
  }

  return 0;
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

int get_empty(int32_t *event_to_fd) {
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (event_to_fd[i] == -1) {
      return i;
    }
  }
  return -1;
}

int get_slot_by_fd(int32_t *event_to_fd, int fd) {
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (event_to_fd[i] == fd) {
      return i;
    }
  }
  return -1;
}

int set_slot_by_fd(int32_t *event_to_fd, int fd, int slot) {
  event_to_fd[slot] = fd;
  return 0;
}

int main(int argc, char *argv[]) {
  int opt;
  uint16_t port = 0;

  while ((opt = getopt(argc, argv, "p:")) != -1) {
    switch (opt) {
      case 'p':
        port = atoi(optarg);
        if (port <= 0 || port > 65535) {
          fprintf(stderr, "Invalid port number");
          exit(EXIT_FAILURE);
        }
        break;
      default:
        fprintf(stderr, "Usage: %s [-p]", argv[0]);
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

  ConnectionInfo client_data[MAX_EVENTS] = {0};
  for (int i = 0; i < MAX_EVENTS; i++) {
    client_data[i].msg = malloc(MAX_MSG_SIZE);
    if (client_data[i].msg == NULL) {
      perror("malloc");
      exit(EXIT_FAILURE);
    }
  }
  int32_t client_to_fd[MAX_EVENTS];
  for (int i = 0; i < MAX_EVENTS; i++) {
    client_to_fd[i] = -1;
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
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
          perror("epoll_ctl add client");
          close(client_fd);
        }
        debug_print("client connected: %d\n", client_fd);

        int empty_slot = get_empty(client_to_fd);
        if (empty_slot < 0) {
          debug_print("too many clients for %d\n", client_fd);
          close(client_fd);
          continue;
        }

        client_data[empty_slot].client_fd = client_fd;
        set_slot_by_fd(client_to_fd, client_fd, empty_slot);
      } else {
        int slot = get_slot_by_fd(client_to_fd, events[i].data.fd);
        if (slot < 0) {
          debug_print("client not found: %d\n", events[i].data.fd);
          continue;
        }
        if (handle_client(&client_data[slot], &events[i], epollfd) < 0) {
          client_to_fd[slot] = -1;
        };
      }
    }
  }

  // clean up
  for (int i = 0; i < MAX_EVENTS; i++) {
    free(client_data[i].msg);
  }

  close(epollfd);
  close(sockfd);
  return 0;
}
