#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdbool.h>
#define PORT 8080
const size_t k_max_msg= 4096;



void error(const char *msg) 
{
    if (errno) {
    perror(msg);
    } else {
    fprintf(stderr, "%s\n", msg);
    }
    exit(EXIT_FAILURE);
}

static int32_t read_full(int fd, char *buf, size_t n)
{
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
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}


static int32_t query(int fd, const char *text)
{
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg) {
        return -1;
    } 
    // send req
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    if (write_all(fd, wbuf, 4 + len) != 0) {
        error("Failed to send req");
    }

    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    if (read_full(fd, rbuf, 4) != 0) {
        error(errno == 0 ? "EOF" : "read() error");
    }

    memcpy(&len,  rbuf, 4);
    if (len > k_max_msg) {
        error("Payload too long");
    }

    // reply body
    if (read_full(fd, &rbuf[4], len)!= 0){
        error("read() error");
    }
    // do something
    printf("server says: %.*s\n", len, &rbuf[4]);
    return 0;

}



int main(void)
{
    int sockfd;
    struct sockaddr_in client_addr;

    // Create a stream socket (TCP)
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("Error opening socket");
    }

    
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = ntohs(PORT);
    client_addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
    int rv = connect(sockfd, (const struct sockaddr *)&client_addr, sizeof(client_addr));
    if (rv < 0) {
        error("We cant connect!!");
    }

    // send multiple requests
    int32_t err = query(sockfd, "hello1");
    if (err) {
        goto L_DONE;
    }
    err = query(sockfd, "hello2");
    if (err) {
        goto L_DONE;
    }
L_DONE:


    close(sockfd);

    return 0;
}

