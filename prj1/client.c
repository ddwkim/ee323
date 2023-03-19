// Client specification
// Your client program should be able to take an IP address,
// a port number of a server and operation type and shift through the command
// line parameters. Example usage of the client program should look like this:$
// ./client -h 143.248.111.222 -p 1234 -o 0 -s 5 In this case, the server’s IP
// address is 143.248.111.222, the server’s port number is 1234, and we are
// encrypting with n=5. You must follow this command line parameters format and
// binary name (which is ‘client’). The client takes the string by standard
// input (stdin). The client then creates the message and sends it to the
// server. Each data should be wrapped in the correct message format that
// follows the protocol above. Once the client receives a reply from the server,
// it should unwrap the message and only print the resulting string to the
// standard output (stdout). Note that if the input is too long (more than 10
// MB), then the client should split the input and wrap it in different
// messages. Otherwise, the server will reject the request. Your client program
// should be able to handle any kind of string, including binary data. Your
// client program should only terminate when EOF (end-of-file) is received in
// stdin.

#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define IN_BUFSIZE 1024
#define MAX_MSG_SIZE 10000000
#define HEADER_SIZE 8
#define MAX_STRING_SIZE 9999992

int sendall(int s, char *buf, int *len) {
  int total = 0;         // how many bytes we've sent
  int bytesleft = *len;  // how many we have left to send
  int n;

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

int recvall(int s, char *buf, uint32_t *len) {
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

char *build_message(char *message, const char *input, uint32_t string_size,
                    uint16_t operation, uint16_t shift) {
  if (string_size > MAX_STRING_SIZE) {
    fprintf(stderr, "Input string too long\n");
    exit(EXIT_FAILURE);
  }

  uint32_t length = htonl(string_size + HEADER_SIZE);

  memcpy(message, &operation, sizeof(operation));
  memcpy(message + sizeof(operation), &shift, sizeof(shift));
  memcpy(message + sizeof(operation) + sizeof(shift), &length, sizeof(length));
  memcpy(message + sizeof(operation) + sizeof(shift) + sizeof(length), input,
         string_size);
  return message;
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
        operation = htons(operation);
        break;
      case 's':
        shift = htons(atoi(optarg));
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
    exit(1);
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
    int pos = 0;
    while (fgets(buffer, sizeof(buffer), stdin)) {
      input = realloc(input, pos + strlen(buffer) + 1);
      if (input == NULL) {
        exit(1);
      }
      strcpy(input + pos, buffer);
      pos += strlen(buffer);
    }

    char *sent = input;
    int n = pos / MAX_STRING_SIZE;
    int r = pos % MAX_STRING_SIZE;
    char *message = malloc(1);
    int req_sent;
    if (n > 0) {
      message = realloc(message, MAX_MSG_SIZE);
      if (message == NULL) {
        perror("realloc");
        exit(1);
      }
      req_sent = MAX_MSG_SIZE;
      for (int i = 0; i < n; i++) {
        message =
            build_message(message, sent, MAX_STRING_SIZE, operation, shift);
        fprintf(stderr, "Message built\n");
        if ((sendall(sockfd, message, &req_sent)) < 0) {
          perror("sendall");
          fprintf(stderr, "sent %d bytes\n", req_sent);
          exit(EXIT_FAILURE);
        }
        sent += MAX_STRING_SIZE;
        fprintf(stderr, "Message sent\n");
      }
    }

    if (r > 0) {
      message = realloc(message, r + HEADER_SIZE);
      if (message == NULL) {
        perror("realloc");
        exit(EXIT_FAILURE);
      }
      req_sent = r + HEADER_SIZE;
      message = build_message(message, sent, r, operation, shift);

      if ((sendall(sockfd, message, &req_sent)) < 0) {
        perror("sendall");
        fprintf(stderr, "sent %d byte\n", req_sent);
        exit(EXIT_FAILURE);
      }
    }

    fprintf(stderr, "Message sent all to host\n");

    char *write = input;
    uint32_t res_recv;
    if (n > 0) {
      res_recv = MAX_MSG_SIZE;
      for (int i = 0; i < n; i++) {
        if ((recvall(sockfd, message, &res_recv)) < 0) {
          perror("recvall");
          fprintf(stderr, "received %d bytes\n", res_recv);
          exit(EXIT_FAILURE);
        }
      }
      parse_message(message, write, &res_recv, &operation, &shift);
      write += res_recv - HEADER_SIZE;
    }

    if (r > 0) {
      res_recv = r + HEADER_SIZE;
      if ((recvall(sockfd, message, &res_recv)) < 0) {
        perror("recvall");
        fprintf(stderr, "received %d bytes\n", res_recv);
        exit(EXIT_FAILURE);
      }
      parse_message(message, write, &res_recv, &operation, &shift);
      write += res_recv - HEADER_SIZE;
    }

    fprintf(stdout, "%s", input);

    free(input);
    free(message);
    close(sockfd);
  }

  return 0;
}