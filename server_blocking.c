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

#define PORT 8081  // Different port so both servers can run
const size_t k_max_msg = 4096;

void error(const char *msg) {
  if (errno) {
    perror(msg);
  } else {
    fprintf(stderr, "%s\n", msg);
  }
  exit(EXIT_FAILURE);
}

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      return -1;
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
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t one_request(int connfd) {
  char rbuf[4 + k_max_msg];
  errno = 0;

  // Read 4-byte length header
  if (read_full(connfd, rbuf, 4) != 0) {
    return -1;  // EOF or error
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    error("payload is too large");
  }

  // Read message body
  if (read_full(connfd, &rbuf[4], len) != 0) {
    error("Failed to read payload");
  }

  printf("Client says: %.*s\n", len, &rbuf[4]);

  // Echo back the same message
  const char *reply = &rbuf[4];
  char wbuf[4 + k_max_msg];
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], reply, len);
  return write_all(connfd, wbuf, 4 + len);
}

int main(void) {
  int sockfd;
  int yes = 1;
  struct sockaddr_in server_addr, client_addr;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    error("Error opening socket");
  }

  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    error("Error setting SO_REUSEADDR");
    close(sockfd);
    return 1;
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(PORT);

  if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    error("Error on binding");
    close(sockfd);
    return 1;
  }

  if (listen(sockfd, SOMAXCONN) < 0) {
    error("Error when trying to listen");
    close(sockfd);
    return 1;
  }

  printf("[BLOCKING SERVER] Listening on port %d. PID: %d\n", PORT, getpid());

  while (true) {
    // BLOCKS here until a client connects
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
      continue;
    }

    printf("Client connected!\n");

    // Handle ALL requests from this client before accepting next client
    while (true) {
      int32_t err = one_request(connfd);
      if (err) {
        break;
      }
    }

    printf("Client disconnected.\n");
    close(connfd);
    // Only NOW can we accept the next client
  }

  close(sockfd);
  return 0;
}
