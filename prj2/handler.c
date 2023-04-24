#include "handler.h"

void reset_proxy_data(proxy_data_t *data) {
  free(data->req_buf);
  free(data->res_buf);
  free(data);
}

void cleanup_and_close(proxy_data_t *data, int epoll_fd) {
  // close file descriptors as needed
  if (data->state & CLIENT_OPEN) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, data->client_fd, NULL);
    close(data->client_fd);
  }
  if (data->state & SERVER_OPEN) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, data->server_fd, NULL);
    close(data->server_fd);
  }
  reset_proxy_data(data);
}

int handle_client(proxy_data_t *data, struct epoll_event *event, int epoll_fd) {
  int *state = &(data->state);

  while (1) {
    if (*state & REQUEST_NOT_RECEIVED) {
      if (data->req_buf_capacity < data->req_buf_used + DELTA) {
        data->req_buf_capacity += CHUNK_SIZE;
        data->req_buf = realloc(data->req_buf, data->req_buf_capacity + 1);
        if (data->req_buf == NULL) {
          perror("realloc");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      }
      ssize_t count = recv(data->client_fd, data->req_buf + data->req_buf_used,
                           data->req_buf_capacity - data->req_buf_used - 1, 0);

      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("recv");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      } else if (count == 0) {
        cleanup_and_close(data, epoll_fd);
        return -1;
      }

      data->req_buf_used += count;
      data->req_buf[data->req_buf_used] = '\0';

      if (strstr(data->req_buf, "\r\n\r\n") != NULL) {
        *state &= ~REQUEST_NOT_RECEIVED;
        *state |= REQUEST_RECEIVED;
        parse_request(data);
      }
    }
    if ((*state & REQUEST_RECEIVED) && !(*state & SERVER_OPEN)) {
      if (!(data->host_entry)) {
        // send 400 Bad Request
        char *res = "HTTP/1.0 400 Bad Request\r\n";
        data->res_buf = malloc(strlen(res) + 1);
        strcpy(data->res_buf, res);
        data->res_buf_used = strlen(res);
        *state &= ~REQUEST_RECEIVED;
        *state |= RESPONSE_RECEIVED;

        // Modify the event to monitor for output readiness
        event->events = EPOLLOUT | EPOLLET;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, data->client_fd, event) == -1) {
          perror("epoll_ctl change mode to out");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }

      } else {
        // connect to the server
        data->server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (data->server_fd == -1) {
          perror("socket");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }

        setnonblocking(data->server_fd);
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(data->port);
        memcpy(&(server_addr.sin_addr), data->host_entry->h_addr_list[0],
               data->host_entry->h_length);
        if (connect(data->server_fd, (struct sockaddr *)&server_addr,
                    sizeof(server_addr)) == -1) {
          if (errno != EINPROGRESS) {
            perror("connect");
            cleanup_and_close(data, epoll_fd);
            return -1;
          }
        }

        // change state for data
        *state |= SERVER_OPEN;

        // register to epoll
        struct epoll_event server_event;
        server_event.events = EPOLLOUT | EPOLLET;
        server_event.data.ptr = data;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, data->server_fd,
                      &server_event) == -1) {
          perror("epoll_ctl");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      }
    }
    if ((*state & REQUEST_RECEIVED) && (*state & SERVER_OPEN)) {
      // send request to server
      ssize_t count = send(data->server_fd, data->req_buf + data->bytes_sent,
                           data->req_buf_used - data->bytes_sent, 0);
      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("send");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      } else if (count == 0) {
        cleanup_and_close(data, epoll_fd);
        return -1;
      }

      data->bytes_sent += count;

      if (data->bytes_sent == data->req_buf_used) {
        data->bytes_sent = 0;
        *state &= ~REQUEST_RECEIVED;
        *state |= REQUEST_SENT;

        // request all sent, watch input from server
        struct epoll_event server_event;
        server_event.events = EPOLLIN | EPOLLET;
        server_event.data.ptr = data;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, data->server_fd,
                      &server_event) == -1) {
          perror("epoll_ctl");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      }
    }
    if (*state & RESPONSE_RECEIVED) {
      // send response to client
      ssize_t count = send(data->client_fd, data->res_buf + data->bytes_sent,
                           data->res_buf_used - data->bytes_sent, 0);
      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("send");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      } else if (count == 0) {
        cleanup_and_close(data, epoll_fd);
        return -1;
      }

      data->bytes_sent += count;

      if (data->bytes_sent == data->res_buf_used) {
        data->bytes_sent = 0;
        *state &= ~RESPONSE_RECEIVED;
        *state |= RESPONSE_SENT;
        cleanup_and_close(data, epoll_fd);
        break;
      }
    }
  }
  return 0;
}

int handle_server(proxy_data_t *data, struct epoll_event *event, int epoll_fd) {
  int *state = &(data->state);

  while (1) {
    if ((*state & REQUEST_SENT)) {
      // receive response from server
      if (data->res_buf_capacity < data->res_buf_used + DELTA) {
        data->res_buf_capacity += CHUNK_SIZE;
        data->res_buf = realloc(data->res_buf, data->res_buf_capacity + 1);
        if (data->res_buf == NULL) {
          perror("realloc");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      }
      ssize_t count = recv(data->server_fd, data->res_buf + data->res_buf_used,
                           data->res_buf_capacity - data->res_buf_used - 1, 0);

      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("recv");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      } else if (count == 0) {
        cleanup_and_close(data, epoll_fd);
        return -1;
      }

      data->res_buf_used += count;
      data->res_buf[data->res_buf_used] = '\0';

      if (!(*state & RESPONSE_HEADER_RECEIVED) &&
          (strstr(data->res_buf, "\r\n\r\n") != NULL)) {
        if (parse_response_header(data) < 0) {
          perror("parse_response_header");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
        *state |= RESPONSE_HEADER_RECEIVED;
      }
      if (*state & RESPONSE_HEADER_RECEIVED) {
        if (data->content_type & CHUNKED) {
          if (strstr(data->res_buf, "\r\n0\r\n\r\n") != NULL) {
            *state &= ~REQUEST_SENT;
            *state |= RESPONSE_RECEIVED;
          }
        } else if (data->content_type & CONTENT_LENGTH) {
          if (data->req_buf_used ==
              (data->content_length + data->header_length)) {
            *state &= ~REQUEST_SENT;
            *state |= RESPONSE_RECEIVED;
          }
        } else {
          *state &= ~REQUEST_SENT;
          *state |= RESPONSE_RECEIVED;
        }

        if (*state & RESPONSE_RECEIVED) {
          // request all sent, watch output readiness from client
          struct epoll_event client_event;
          client_event.events = EPOLLOUT | EPOLLET;
          client_event.data.ptr = data;
          if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, data->client_fd,
                        &client_event) == -1) {
            perror("epoll_ctl");
            cleanup_and_close(data, epoll_fd);
            return -1;
          }
        }
      }
    }
    if (*state & RESPONSE_RECEIVED) {
      // send response to client
      ssize_t count = send(data->client_fd, data->res_buf + data->bytes_sent,
                           data->res_buf_used - data->bytes_sent, 0);
      if (count == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          perror("send");
          cleanup_and_close(data, epoll_fd);
          return -1;
        }
      } else if (count == 0) {
        cleanup_and_close(data, epoll_fd);
        return -1;
      }

      data->bytes_sent += count;

      if (data->bytes_sent == data->res_buf_used) {
        data->bytes_sent = 0;
        *state &= ~RESPONSE_RECEIVED;
        *state |= RESPONSE_SENT;
        cleanup_and_close(data, epoll_fd);
        break;
      }
    }
  }
  return 0;
}

void parse_request(proxy_data_t *data) {
  // check if the request is GET
  // check if the request is HTTP/1.0
  if (strncmp(data->req_buf, "GET", 3) != 0 ||
      strstr(data->req_buf, "HTTP/1.0") == NULL) {
    data->host_entry = NULL;
    return;
  }
  // check if the request has Host header
  char *host = strstr(data->req_buf, "Host: ");
  if (host == NULL) {
    data->host_entry = NULL;
    return;
  }
  // check if the host is valid
  host += 6;
  char *end = strstr(host, "\r\n");
  if (end == NULL) {
    data->host_entry = NULL;
    return;
  }
  *end = '\0';
  // find port
  char *port = strstr(host, ":");
  if (port != NULL) {
    *port = '\0';
    port++;
    data->port = atoi(port);
  } else {
    data->port = 80;
  }

  struct hostent *host_entry = gethostbyname(host);
  *end = '\r';
  data->host_entry = host_entry;

  return;
}

int parse_response_header(proxy_data_t *data) {
  char *header_end = strstr(data->res_buf, "\r\n\r\n") + 4;

  char *content_length_header = strstr(data->res_buf, "Content-Length:");
  if (content_length_header != NULL) {
    if (content_length_header != NULL) {
      char *start = content_length_header + strlen("Content-Length:");
      char *end;
      data->content_length = strtol(start, &end, 10);
      if (start == end) {
        // Error: Content-Length value is missing or invalid
        return -1;
      }
    }
    data->content_type = CONTENT_LENGTH;
  }

  char *transfer_encoding_header = strstr(data->res_buf, "Transfer-Encoding:");
  if (transfer_encoding_header != NULL &&
      strstr(transfer_encoding_header, "chunked") != NULL) {
    data->content_type = CHUNKED;
  }

  data->header_length = header_end - data->res_buf;

  return 0;
}