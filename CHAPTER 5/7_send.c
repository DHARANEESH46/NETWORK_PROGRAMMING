#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#define PORT "3490"
#define BUF_SIZE 1024

int main(void)
{
    int sockfd;
    struct addrinfo hints, *res, *p;
    char buf[BUF_SIZE];
    int num;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    getaddrinfo("127.0.0.1", PORT, &hints, &res);

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
            continue;

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }

    char *msg = "Hello,I am client";
    send(sockfd, msg, strlen(msg), 0);

    num = recv(sockfd, buf, BUF_SIZE - 1, 0);
    if (num == -1) {
        perror("recv");
        close(sockfd);
        return 1;
    }

    buf[num] = '\0';
    printf("Message received from server : %s\n", buf);

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}
