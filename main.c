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
  /* fcntl() = "file control" - manipulates file descriptor properties
   * F_GETFL = "get flags"  - retrieve cur flags for this fd
   * The 0 is a placeholder argument (not used for F_GETFL)
   */
  int flags = fcntl(fd, F_GETFL, 0);
  /* F_SETFL = "set flags" - apply new flags to this fd
   * flags | O_NONBLOCK = add non-blocking flag to existing flags
   * Using bitwise OR (|) preserves all existing flags while adding O_NONBLOCK
   */
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
  *len -= n;
}

// Implement handle_accept()
// This function accepts a new client connection and creats a Conn object for
// it.

static Conn *handle_accept(int listen_fd) {
  // accept incoming connection
  struct sockaddr_in client_addr;
  socklen_t addrlen = sizeof(client_addr);
  int connfd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
  if (connfd < 0) {
    return NULL; // No pending connection or error
  }
  // Set the new initialize connection state
  fd_set_nb(connfd);

  // Allocate and initialize connection state
  Conn *conn = malloc(sizeof(Conn));
  conn->fd = connfd;
  conn->want_read = true; // Start by waiting for client data
  conn->want_write = false;
  conn->want_close = false;
  conn->incoming = NULL;
  conn->incoming_len = 0;
  conn->incoming_cap = 0;
  conn->outgoing = NULL;
  conn->outgoing_len = 0;
  conn->outgoing_cap = 0;

  return conn;
}

/*
  Implement try_one_request()

  What it does:
  1. Checks if we have at least 4 bytes (length header)
  2. Reads the length, validates it's not too large
  3. Checks if the full message has arrived
  4. Processes the request (prints it, builds echo response)
  5. Removes the processed bytes from incoming buffer
  6. Returns true if a request was processed (so handle_read can loop)


  This parses one request from the buffer. Add it before handle_read() (since
  handle_read calls it):

*/
static bool try_one_request(Conn *conn) {
  // Need at least 4 bytes for the length header
  if (conn->incoming_len < 4) {
    return false;
  }

  // Read the length (first 4 bytes)
  uint32_t len = 0;
  memcpy(&len, conn->incoming, 4);

  // Sanity check
  if (len > k_max_msg) {
    conn->want_close = true;
    return false;
  }

  // Check if we have the full message (4-byte header + payload)
  if (conn->incoming_len < 4 + len) {
    return false; // incomplete, wait for more data
  }

  // We have a complete request - process it
  // For now, just echo back what we received
  printf("Client says: %.*s\n", len, conn->incoming + 4);

  // Build response (echo the same message back)
  buf_append(&conn->outgoing, &conn->outgoing_len, &conn->outgoing_cap,
             (uint8_t *)&len, 4);
  buf_append(&conn->outgoing, &conn->outgoing_len, &conn->outgoing_cap,
             conn->incoming + 4, len);

  // Remove processed request from incoming buffer
  buf_consume(&conn->incoming, &conn->incoming_len, 4 + len);

  return true; // successfully processed one request
}

// Implement handle_read()
// This function reads data from a client socket into the incoming buffer.
/*   1. Reads whatever data is available (non-blocking)
  2. If connection closed or error, mark want_close = true
  3. Appends data to conn->incoming buffer
  4. Tries to parse complete requests with try_one_request()
  5. If there's a response to send, switches from read mode to write mode
*/

static void handle_read(Conn *conn) {
  // temporary buffer for reading
  uint8_t buf[64 * 1024]; // 64KB read buffer
  // non-blocking read
  ssize_t rv = read(conn->fd, buf, sizeof(buf));

  if (rv <= 0) {
    // rv == 0: cli closed connection (EOF)
    // rv <= 0: erro (could be EAGAIN, but we only call this when poll says
    // ready)
    conn->want_close = true;
    return;
  }
  // append received data to incoming buffer
  buf_append(&conn->incoming, &conn->incoming_len, &conn->incoming_cap, buf,
             (size_t)rv);
  // Try to process complete requests from the buffer
  while (try_one_request(conn)) {
    // keep processing until no complete request remains
  }
  // if we have data to send, switch to write mode
  if (conn->outgoing_len > 0) {
    conn->want_read = false;
    conn->want_write = true;
  }
}

/*
  Implement handle_write()
  What it does:
  1. Writes as much data as possible to the socket (non-blocking, may be
  partial)
  2. If error, mark connection for closing
  3. Removes written bytes from the buffer using buf_consume()
  4. When buffer is empty, switches back to read mode to wait for next request

  This function sends data from the outgoing buffer to the client. Add after
  handle_read():

*/

static void handle_write(Conn *conn) {
  // Try to write as much as possible from outgoing buffer
  ssize_t rv = write(conn->fd, conn->outgoing, conn->outgoing_len);

  if (rv < 0) {
    // Write error, close connection
    conn->want_close = true;
    return;
  }

  // Remove the bytes that were successfully written
  buf_consume(&conn->outgoing, &conn->outgoing_len, (size_t)rv);

  // If all data has been sent, switch back to read mode
  if (conn->outgoing_len == 0) {
    conn->want_read = true;
    conn->want_write = false;
  }
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

  // Listen for connections
  if (listen(sockfd, SOMAXCONN) < 0) {
    error("Error when trying to listen");
    close(sockfd);
    return 1;
  }

  // Set listening socket to non-blocking
  fd_set_nb(sockfd);

  printf("Server listening on port %d. PID: %d\n", PORT, getpid());

  // ===== EVENT LOOP =====
  while (true) {
    // Step 1: Build poll arguments
    // Count active connections + 1 for listening socket
    int nfds = 1;
    for (size_t i = 0; i < fd2conn_size; i++) {
      if (fd2conn[i] != NULL) {
        nfds++;
      }
    }

    // Allocate pollfd array
    struct pollfd *pfds = malloc(nfds * sizeof(struct pollfd));
    int idx = 0;

    // First entry: listening socket
    pfds[idx].fd = sockfd;
    pfds[idx].events = POLLIN; // Watch for incoming connections
    pfds[idx].revents = 0;
    idx++;

    // Add entries for each active connection
    for (size_t i = 0; i < fd2conn_size; i++) {
      Conn *conn = fd2conn[i];
      if (!conn)
        continue;

      pfds[idx].fd = conn->fd;
      pfds[idx].events = POLLERR; // Always watch for errors
      if (conn->want_read) {
        pfds[idx].events |= POLLIN;
      }
      if (conn->want_write) {
        pfds[idx].events |= POLLOUT;
      }
      pfds[idx].revents = 0;
      idx++;
    }

    // Step 2: Call poll() - the ONLY blocking syscall
    int rv = poll(pfds, nfds, -1); // -1 = wait forever
    if (rv < 0) {
      if (errno == EINTR) {
        free(pfds);
        continue; // Interrupted by signal, retry
      }
      perror("poll");
      free(pfds);
      break;
    }

    // Step 3: Accept new connections
    if (pfds[0].revents & POLLIN) {
      Conn *conn = handle_accept(sockfd);
      if (conn) {
        // Expand fd2conn array if needed
        if ((size_t)conn->fd >= fd2conn_size) {
          size_t new_size = conn->fd + 1;
          fd2conn = realloc(fd2conn, new_size * sizeof(Conn *));
          // Initialize new slots to NULL
          for (size_t j = fd2conn_size; j < new_size; j++) {
            fd2conn[j] = NULL;
          }
          fd2conn_size = new_size;
        }
        fd2conn[conn->fd] = conn;
      }
    }

    // Step 4: Process ready connections
    for (int i = 1; i < nfds; i++) {
      if (pfds[i].revents == 0)
        continue;

      Conn *conn = fd2conn[pfds[i].fd];
      if (!conn)
        continue;

      if (pfds[i].revents & POLLIN) {
        handle_read(conn);
      }
      if (pfds[i].revents & POLLOUT) {
        handle_write(conn);
      }
    }

    // Step 5: Close connections that are done or errored
    for (int i = 1; i < nfds; i++) {
      Conn *conn = fd2conn[pfds[i].fd];
      if (!conn)
        continue;

      short re = pfds[i].revents;
      if ((re & (POLLERR | POLLHUP | POLLNVAL)) || conn->want_close) {
        close(conn->fd);
        free(conn->incoming);
        free(conn->outgoing);
        fd2conn[conn->fd] = NULL;
        free(conn);
      }
    }

    free(pfds);
  }

  close(sockfd);
  return 0;
}
