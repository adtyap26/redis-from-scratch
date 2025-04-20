#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define PORT 8080
const size_t k_max_msg = 4096;

void error(const char *msg) {
  if (errno) {
    perror(msg);
  } else {
    fprintf(stderr, "%s\n", msg);
  }
  exit(EXIT_FAILURE);
}

// static void do_something(int connfd) {
//     char rbuf[64] = {};
//     ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
//     if (n < 0) {
//         error("read() error");
//         return;
//     }
//     printf("client says: %s\n", rbuf);
//
//     char wbuf[] = "world";
//     write(connfd, wbuf, strlen(wbuf));
// }

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      return -1; // error, or unexpected EOF
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv <= 0) {
      return -1; // error
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t one_request(int connfd) {

  char rbuf[4 + k_max_msg]; // buffer for length + payload
  errno = 0;

  if (read_full(connfd, rbuf, 4) != 0) {
    error(errno == 0 ? "EOF" : "Failed to read length");
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    error("payload is too large");
  }
  // req body
  if (read_full(connfd, &rbuf[4], len) != 0) {
    error("Failed to read payload");
  }

  printf("Client says: %.*s\n", len, &rbuf[4]);
  const char reply[] = "world";
  char wbuf[4 + sizeof(reply)];
  len = (uint32_t)strlen(reply);
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], reply, len);
  return write_all(connfd, wbuf, 4 + len);
}

int main(void) {
  int sockfd;
  int yes = 1;
  struct sockaddr_in server_addr, client_addr;

  // Create a stream socket (TCP)
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    error("Error opening socket");
  }

  // Set the SO_REUSEADDR option
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    error("Error setting SO_REUSEADDR");
    close(sockfd);
  }

  // Initialize server address structure
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(0); // wildcard set to 0.0.0.0
  server_addr.sin_port = htons(PORT);

  // Bind the socket
  if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    error("Error on binding");
  }

  // Listen for connections
  if (listen(sockfd, SOMAXCONN) < 0) {
    error("Error when try to listen");
  }

  printf("Server listening on port %d. PID: %d\n", PORT, getpid());

  while (true) {
    // accept
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
      continue; // error
    }

    while (true) {
      int32_t err = one_request(connfd);
      if (err) {
        break;
      }
    }
    close(connfd);
  }

  close(sockfd);
  return 0;
}
