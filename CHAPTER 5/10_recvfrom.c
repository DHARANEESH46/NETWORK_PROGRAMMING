#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT "3490"
#define BUFSIZE 1024

int main()
{
    int sockfd,status;
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    char buf[BUFSIZE];
    int numbytes;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;      
    hints.ai_socktype = SOCK_DGRAM;   
    hints.ai_flags = AI_PASSIVE;      

    if ((status=getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }

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

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    freeaddrinfo(res);

    printf("Waiting for data...\n");

    addr_len = sizeof their_addr;
    numbytes = recvfrom(sockfd, buf, BUFSIZE - 1, 0,(struct sockaddr *)&their_addr, &addr_len);

    if (numbytes == -1) {
        perror("recvfrom");
        close(sockfd);
        return 3;
    }

    buf[numbytes] = '\0';
    printf("Received from client : %s\n", buf);

    char *reply = "Successfully received your message";
    sendto(sockfd,reply, strlen(reply), 0,(struct sockaddr *)&their_addr, addr_len);

    close(sockfd);
    return 0;
}
