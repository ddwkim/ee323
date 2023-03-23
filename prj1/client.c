#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

int sendall(uint32_t s, char *buf, uint32_t *len) {
  uint32_t total = 0;         // how many bytes we've sent
  uint32_t bytesleft = *len;  // how many we have left to send
  uint32_t n;

  while (total < *len) {
    n = send(s, buf + total, bytesleft, 0);
    if (n == -1) {
      break;
    }
    total += n;
    bytesleft -= n;
  }

  *len = total;  // return number actually sent here

  return n == -1 ? -1 : 0;  // return -1 on failure, 0 on success
}

int recvall(uint32_t s, char *buf, uint32_t *len) {
  uint32_t total = 0;         // how many bytes we've received
  uint32_t bytesleft = *len;  // how many we have left to receive
  uint32_t n;

  while (total < *len) {
    n = recv(s, buf + total, bytesleft, 0);
    if (n == -1) {
      break;
    }
    total += n;
    bytesleft -= n;
  }

  *len = total;  // return number actually received here

  return n == -1 ? -1 : 0;  // return -1 on failure, 0 on success
}

void build_message(char *message, const char *input, uint32_t string_size,
                   uint16_t operation, uint16_t shift) {
  if (string_size > MAX_STRING_SIZE) {
    fprintf(stderr, "Input string too long : %d\n", string_size);
    exit(EXIT_FAILURE);
  }

  uint16_t op = htons(operation);
  uint16_t sh = htons(shift);
  uint32_t msg_size = htonl(string_size + HEADER_SIZE);

  memcpy(message, &op, sizeof(op));
  memcpy(message + sizeof(op), &sh, sizeof(sh));
  memcpy(message + sizeof(op) + sizeof(sh), &msg_size, sizeof(msg_size));
  memcpy(message + sizeof(op) + sizeof(sh) + sizeof(msg_size), input,
         string_size);
}

void parse_message(char *message, char *target, uint32_t *msg_length,
                   uint16_t *operation, uint16_t *shift) {
  memcpy(operation, message, sizeof(*operation));
  memcpy(shift, message + sizeof(*operation), sizeof(*shift));
  memcpy(msg_length, message + sizeof(*operation) + sizeof(*shift),
         sizeof(*msg_length));

  *operation = ntohs(*operation);
  *shift = ntohs(*shift);
  *msg_length = ntohl(*msg_length);

  memcpy(target, message + HEADER_SIZE, *msg_length - HEADER_SIZE);
}

int main(int argc, char *argv[]) {
  int opt;
  char *address;
  uint16_t operation, shift, port;

  while ((opt = getopt(argc, argv, "h:p:o:s:")) != -1) {
    switch (opt) {
      case 'h':
        address = optarg;
        break;
      case 'p':
        port = atoi(optarg);
        if (port <= 0 || port > 65535) {
          fprintf(stderr, "Invalid port number\n");
          exit(EXIT_FAILURE);
        }
        break;
      case 'o':
        operation = atoi(optarg);
        if (operation != 0 && operation != 1) {
          fprintf(stderr, "Invalid operation\n");
          fprintf(stderr, "0 for encryption 1 for decryption\n");
          exit(EXIT_FAILURE);
        }
        break;
      case 's':
        shift = atoi(optarg);
        break;
      default:
        fprintf(stderr, "Usage: %s [-h] [-p] [-o] [-s]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  // connect to server
  struct sockaddr_in saddr;

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

  struct hostent *hp;
  if ((hp = gethostbyname(address)) == NULL) {
    perror("gethostbyname");
    exit(EXIT_FAILURE);
  }

  saddr.sin_family = AF_INET;
  memcpy(&saddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
  saddr.sin_port = htons(port);
  if (connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    perror("connect");
    exit(EXIT_FAILURE);
  }

  int start;
  while (1) {
    if ((start = fgetc(stdin)) == EOF) {
      break;
    } else {
      ungetc(start, stdin);
    }

    // read from stdin
    char buffer[IN_BUFSIZE];
    char *input = malloc(1);
    uint32_t pos = 0;
    while (fgets(buffer, sizeof(buffer), stdin)) {
      input = realloc(input, pos + strlen(buffer) + 1);
      if (input == NULL) {
        exit(EXIT_FAILURE);
      }
      strcpy(input + pos, buffer);
      pos += strlen(buffer);
    }
    input[pos] = '\0';

    char *sent = input;
    uint32_t n = pos / MAX_STRING_SIZE;
    uint32_t r = pos % MAX_STRING_SIZE;
    char *message = malloc(MAX_MSG_SIZE);
    uint32_t req_sent;

    char *write = input;
    uint32_t res_recv;

    if (n > 0) {
      message = realloc(message, MAX_MSG_SIZE);
      if (message == NULL) {
        perror("realloc");
        exit(EXIT_FAILURE);
      }
      req_sent = MAX_MSG_SIZE;
      for (int i = 0; i < n; i++) {
        build_message(message, sent, MAX_STRING_SIZE, operation, shift);
        DEBUG_PRINT("Message built %d\n", MAX_MSG_SIZE);
        if ((sendall(sockfd, message, &req_sent)) < 0) {
          perror("sendall");
          fprintf(stderr, "sent %d bytes\n", req_sent);
          exit(EXIT_FAILURE);
        }
        sent += MAX_STRING_SIZE;
        DEBUG_PRINT("Message sent real %d\n", req_sent);

        res_recv = MAX_MSG_SIZE;
        if ((recvall(sockfd, message, &res_recv)) < 0) {
          perror("recvall");
          fprintf(stderr, "received %d bytes\n", res_recv);
          exit(EXIT_FAILURE);
        }
        parse_message(message, write, &res_recv, &operation, &shift);
        DEBUG_PRINT("Message received real %d\n", res_recv);

        write += res_recv - HEADER_SIZE;
      }
    }

    if (r > 0) {
      message = realloc(message, r + HEADER_SIZE);
      if (message == NULL) {
        perror("realloc");
        exit(EXIT_FAILURE);
      }
      req_sent = r + HEADER_SIZE;
      build_message(message, sent, r, operation, shift);

      if ((sendall(sockfd, message, &req_sent)) < 0) {
        perror("sendall");
        fprintf(stderr, "sent %d byte\n", req_sent);
        exit(EXIT_FAILURE);
      }
      sent += r;
      DEBUG_PRINT("Message sent real %d\n", req_sent);

      res_recv = r + HEADER_SIZE;
      if ((recvall(sockfd, message, &res_recv)) < 0) {
        perror("recvall");
        fprintf(stderr, "received %d bytes\n", res_recv);
        exit(EXIT_FAILURE);
      }
      parse_message(message, write, &res_recv, &operation, &shift);
      DEBUG_PRINT("Message received real %d\n", res_recv);
      write += res_recv - HEADER_SIZE;
    }

    fprintf(stdout, "%s", input);

    free(input);
    free(message);
  }
  close(sockfd);

  return 0;
}