#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
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

/* read_full() and write_all() are only for blocking without event loop
 * this is on 4.1 Multiple requests in a single connection

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
*/

/* This struct is implemented for event loop scenario
 * This is on 06. Event Loop > 6.1 introduction
 * */
typedef struct {
  int fd;              // client socket file descriptor
  bool want_read;      // application wants to read (fill incoming buffer)
  bool want_write;     // application wants to write (flush outgoing buffer)
  bool want_close;     // application wants to close the connection
  uint8_t *incoming;   // dynamically-allocated input buffer >> on c++ it use
                       // vector -> std::vector<uint8_t> incoming
  size_t incoming_len; // current length of data in incoming buffer
  size_t incoming_cap; // current capacity of incoming buffer
  uint8_t *outgoing;   // dynamically-allocated output buffer  ->
                       // std::vector<uint8_t> outgoing
  size_t outgoing_len; // current length of data in outgoing buffer
  size_t outgoing_cap; // current capacity of outgoing buffer
} Conn;

// Global connection tracking
static Conn **fd2conn = NULL;
static size_t fd2conn_size = 0;

static void fd_set_nb(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void buf_append(uint8_t **buf, size_t *len, size_t *cap,
                       const uint8_t *data, size_t n) {
  if (*len + n > *cap) {
    // code below actually looks like this in case you forget in the future.
    // size_t new_cap;
    // if (*cap == 0) {
    //     new_cap = 64;
    // } else {
    //     new_cap = *cap * 2;
    // }
    size_t new_cap = (*cap == 0) ? 64 : *cap * 2;
    while (new_cap < *len + n) {
      new_cap *= 2;
    }
    *buf = realloc(*buf, new_cap);
    *cap = new_cap;
  }
  memcpy(*buf + *len, data, n);
  *len += n;
}

static void buf_consume(uint8_t **buf, size_t *len, size_t n) {
  memmove(*buf, *buf + n, *len - n);
  *len + n;
}

/* Start each iteration into the main loop
 * Listen to socket to accept connection
 * We add entry per active client Conn
 * We check each Conn to see if its non-null
 * Finnaly we set events based want_read and want_write
 */

// void eventLoop(void) {
//   while (true) {
//     // count how many fds we will poll (1 for listening + one per active
//     // connection)
//     int fds = 1;
//     for (size_t i = 0; i < fd2conn_size; i++) {
//       if (fd2conn[i] != NULL)
//         nfds++;
//     }
//     // we allocate pollfd array
//     struct pollfd *pfds = malloc(nfds * sizeof(struct pollfd));
//     int idx = 0;
//
//     // listening socket - watch for read (incoming connection)
//     pfds[idx].fd = listen_fd;
//     pfds[idx].events = POLLIN;
//     pfds[idx].revents = 0;
//     idx++;
//
//     for (size_t i = 0; i < fd2conn_size; i++) {
//       Conn *conn = fd2conn[i];
//       if (!conn)
//         continue;
//       pfds[idx].fd = conn->fd;
//       // always check for errors or hangups
//       pfds[idx].events = POLLERR;
//       if (conn->want_read)
//         pfds[idx].events |= POLLIN;
//       if (conn->want_write)
//         pfds[idx].events |= POLLOUT;
//       pfds[idx].revents = 0;
//       idx++
//     };
//     // Step 2 call poll()
//     int rv = poll(pfds, nfds, -1); // timeout -1 means to waint
//     indefinitely if (rv < 0) {
//       if (errno == EINTR) {
//         // interupted by signal, retry
//         free(pfds);
//         continue;
//       }
//       perror("poll");
//       exit(1);
//     }
//
//     // accept new connection
//     if (pfds[0].revents & POLLIN) {
//       Conn *conn = handle_accept(listen_fd);
//       if (conn) {
//         // expand fd2conn if neccessary
//         if ((size_t)conn->fd >= fd2conn_size) {
//           size_t new_size = conn->fd + 1;
//           fd2conn = realloc(fd2conn, new_size * sizeof(Conn *));
//           for (size_t = fd2conn_size; j < new_size; j++) {
//             fd2conn[j] = NULL
//           }
//           fd2conn_size = new_size;
//         }
//         fd2conn[conn->fd] = conn;
//       }
//     }
//
//     // Step 4. Invoke application callbacks
//     for (int i = 1; i < nfds; i++) {
//       Conn *conn = fd2conn[pfds[i].fd];
//       if (!conn)
//         continue;
//
//       if (pfds[i].revents & POLLIN) {
//         handle_read(conn); // perform non-blocking read and protocol
//         parsing
//       }
//       if (pfds[i].revents & POLLOUT) {
//         handle_write(conn); // attempt non-blocking write of outgoing
//         buffer
//       }
//     }
//
//     // Step 5. Terminate connections
//     for (int i = 1; i < nfds; i++) {
//       int fd = pfds[i].fd;
//       Conn *conn = fd2conn[fd];
//       if (!conn)
//         continue;
//       short re = pfds[i].revents;
//
//       if ((re & (POLLERR | POLLHUP | POLLNVAL)) || conn->want_close) {
//         // Clean up connection
//         close(conn->fd);
//         free(conn->incoming);
//         free(conn->outgoing);
//         free(conn);
//         fd2conn[fd] = NULL;
//       }
//     }
//     free(pfds);
//     // Loop continues...
//   }
// }

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
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // clearer than htonl(0)
  server_addr.sin_port = htons(PORT);

  if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    error("Error on binding");
    close(sockfd);
    return 1;
  }

  //   // if (listen(sockfd, SOMAXCONN) < 0) {
  //   //   error("Error when trying to listen");
  //   //   close(sockfd);
  //   //   return 1;
  //   // }
  //   //
  //   // printf("Server listening on port %d. PID: %d\n", PORT, getpid());
  //   //
  //   // while (true) {
  //   //   socklen_t addrlen = sizeof(client_addr);
  //   //
  //   //   int connfd = accept(sockfd, (struct sockaddr *)&client_addr,
  //   &addrlen);
  //   //   if (connfd < 0) {
  //   //     continue;
  //   //   }
  //
  //   while (true) {
  //     int32_t err = one_request(connfd);
  //     if (err) {
  //       break;
  //     }
  //   }
  //
  //   close(connfd);
  // }

  close(sockfd);
  return 0;
}
