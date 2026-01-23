#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#define PORT "3490"
#define BACKLOG 10
#define BUF_SIZE 1024

int main(void)
{
    int sockfd, new_fd;
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    char buf[BUF_SIZE];
    int numbytes;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, PORT, &hints, &res);

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
            continue;

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }

    listen(sockfd, BACKLOG);

    printf("Server waiting for connection...\n");

    addr_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);

    numbytes = recv(new_fd, buf, BUF_SIZE - 1, 0);
    if (numbytes == -1) {
        perror("recv");
        close(new_fd);
        close(sockfd);
        return 1;
    }

    buf[numbytes] = '\0';
    printf("Message received from client : %s\n", buf);

    char *reply = "Successfully received your message";
    send(new_fd, reply, strlen(reply), 0);

    close(new_fd);
    close(sockfd);
    freeaddrinfo(res);
    return 0;
}
