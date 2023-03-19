// Your server should receive the required information through the command line
// parameters. An example usage of the server program should look like this:$
// ./server -p 1234 Unlike the client, when you run your server, you should be
// able to set the server’s port number manually. In fact, it is very natural
// that by setting the server’s port number with the desired value we can
// advertise the port number to the clients out there. You must follow this
// command line parameters format and binary name (which is ‘server’). The
// server program listens to incoming connection requests and then accepts them.
// Once the server program receives a string from the client, the server
// performs the Caesar cipher on this string and sends the resulting message
// back to the client. Your server program must be able to handle multiple
// connections in parallel (up to 50). You have several options for this
// requirement, like fork() or select()/epoll(). Your server should reject
// connections from clients that violate the protocol.

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

#define BUFSIZE 1024
#define MAX_MSG_SIZE 10000000
#define HEADER_SIZE 8
#define MAX_EVENTS 50
#define BACKLOG 10

typedef struct {
  int client_fd;
  uint16_t op;
  uint16_t shift;
  uint32_t msg_size;
  uint32_t bytes_recv;
  uint32_t bytes_sent;
  char *msg;
} ConnectionInfo;

void caesar_cipher(char *buffer, uint32_t len, uint16_t shift, uint16_t op) {
  if (op == 1) {
    shift = 26 - shift;
  }

  for (int i = HEADER_SIZE - 1; i < len; i++) {
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
}

void handle_client(ConnectionInfo *client_data, struct epoll_event *event,
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
          perror("recv");
          close(fd);
          reset_client_data(client_data);
          return;
        }
      } else if (count == 0) {
        close(fd);
        reset_client_data(client_data);
        return;
      }

      *bytes_recv += count;

      // Fill metadata
      if (*bytes_recv == HEADER_SIZE) {
        client_data->op = ntohs(*((uint16_t *)msg));
        client_data->shift = ntohs(*((uint16_t *)(msg + 2)));
        *msg_size = ntohl(*((uint32_t *)(msg + 4)));

        if (*msg_size > MAX_MSG_SIZE) {
          fprintf(stderr, "Message too large\n");
          close(fd);
          reset_client_data(client_data);
          return;
        }
      }
    }

    // Header received, Keep reading until we have a full message
    if (*msg_size > 0 && *bytes_recv < *msg_size) {
      ssize_t count = recv(fd, msg + *bytes_recv, *msg_size - *bytes_recv, 0);

      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("recv");
          close(fd);
          reset_client_data(client_data);
          return;
        }
      } else if (count == 0) {
        close(fd);
        reset_client_data(client_data);
        return;
      }

      *bytes_recv += count;
    }

    // Process the message
    else if (*msg_size > 0 && *bytes_recv == *msg_size) {
      caesar_cipher(msg, *msg_size, client_data->shift, client_data->op);
      *bytes_sent = 0;

      // Modify the event to monitor for output readiness
      event->events = EPOLLOUT | EPOLLET;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_data->client_fd, event) ==
          -1) {
        perror("epoll_ctl");
        reset_client_data(client_data);
        close(fd);
        return;
      }
    }
    ssize_t count =
        send(fd, client_data->msg + *bytes_sent, *msg_size - *bytes_sent, 0);
    if (count == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Modify the event to monitor for output readiness
        event->events = EPOLLOUT | EPOLLET;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_data->client_fd, event) ==
            -1) {
          perror("epoll_ctl");
          reset_client_data(client_data);
          close(fd);
          return;
        }
        break;
      } else {
        perror("send");
        reset_client_data(client_data);
        close(fd);
        return;
      }
    }

    *bytes_sent += count;
    if (*bytes_sent == *msg_size) {
      // sending finished, modify the event to monitor for input readiness
      event->events = EPOLLIN | EPOLLET;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_data->client_fd, event) ==
          -1) {
        perror("epoll_ctl");
        reset_client_data(client_data);
        close(fd);
        return;
      }
      reset_client_data(client_data);
      break;
    }
  }
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
    exit(1);
  }

  int yes = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    perror("setsockopt");
    close(sockfd);
    exit(1);
  }

  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = INADDR_ANY;
  saddr.sin_port = htons(port);
  if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    perror("bind");
    exit(1);
  }

  if (listen(sockfd, BACKLOG) < 0) {
    perror("listen");
    exit(1);
  }

  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    perror("epoll_create1");
    exit(1);
  }

  struct epoll_event ev, events[MAX_EVENTS];
  ev.events = EPOLLIN;
  ev.data.fd = sockfd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
    perror("epoll_ctl: listen_sock");
    exit(1);
  }

  ConnectionInfo client_data[MAX_EVENTS] = {0};
  for (int i = 0; i < MAX_EVENTS; i++) {
    client_data->msg = malloc(MAX_MSG_SIZE);
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
          perror("epoll_ctl");
          close(client_fd);
        }

        client_data[client_fd].client_fd = client_fd;
      } else {
        handle_client(&client_data[events[i].data.fd], &events[i], epollfd);
      }
    }
  }

  for (int i = 0; i < MAX_EVENTS; i++) {
    free(client_data[i].msg);
  }

  close(epollfd);
  close(sockfd);
  return 0;
}
