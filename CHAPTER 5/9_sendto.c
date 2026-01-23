#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT "3490"
#define IP "127.0.0.1"
#define BUFSIZE 1024

int main()
{
    int sockfd,status;
    struct addrinfo hints, *res, *p;
    int numbytes;
    char buf[BUFSIZE];
    struct sockaddr_storage server_addr;
    socklen_t addr_len;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;      
    hints.ai_socktype = SOCK_DGRAM;   

    if ((status=getaddrinfo(IP, PORT, &hints, &res) )!= 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 2;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd != -1)
            break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to create socket\n");
        return 3;
    }

    sendto(sockfd, "Hello,Iam UDP", 16, 0,p->ai_addr, p->ai_addrlen);

    addr_len = sizeof server_addr;
    numbytes = recvfrom(sockfd, buf, BUFSIZE - 1, 0,(struct sockaddr *)&server_addr, &addr_len);

    if (numbytes == -1) {
        perror("recvfrom");
        close(sockfd);
        return 4;
    }

    buf[numbytes] = '\0';
    printf("Received from server : %s\n", buf);

    freeaddrinfo(res);
    close(sockfd);
    return 0;
}
