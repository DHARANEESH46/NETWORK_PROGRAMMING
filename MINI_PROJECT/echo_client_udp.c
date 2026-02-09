#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT "3490"
#define MAXLINE 50

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc,char *argv[])
{
    struct addrinfo hints, *res, *p;
    int sockfd, status;
    char msg[MAXLINE];
    char rec[MAXLINE];
    char s[INET6_ADDRSTRLEN];

    if (argc != 2) {
        fprintf(stderr,"usage: client hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((status = getaddrinfo(argv[1], PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
            continue;
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "failed to create socket\n");
        freeaddrinfo(res);
        return 1;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),s, sizeof s);
    printf("UDP client sending to %s\n", s);

    while (1) {
        printf("ENTER THE MESSAGE (type exit to quit): ");
        fgets(msg, sizeof msg, stdin);

        for (int i = 0; msg[i]; i++)
            if (msg[i] == '\n') {
                msg[i] = '\0';
                break;
            }

        if (strcmp(msg, "exit") == 0)
            break;

        time_t now = time(NULL);
        strcat(msg, " | Timestamp: ");
        strcat(msg, ctime(&now));

        sendto(sockfd, msg, strlen(msg) , 0,p->ai_addr, p->ai_addrlen);

        int num = recvfrom(sockfd, rec, MAXLINE - 1, 0, NULL, NULL);
        if (num <= 0)
            break;

        rec[num] = '\0';
        printf("Echo from server: %s\n", rec);
    }

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}
