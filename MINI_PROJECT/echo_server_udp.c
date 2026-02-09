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
#define MAXLINE 40

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main()
{
    struct addrinfo hints, *res, *p;
    int sockfd, status;
    struct sockaddr_storage client_addr;
    socklen_t addrlen;
    char rec[MAXLINE];
    char sen[MAXLINE];
    char s[INET6_ADDRSTRLEN];
    int count = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
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
        fprintf(stderr, "failed to bind socket\n");
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);

    printf("UDP Echo Server waiting on port %s...\n", PORT);

    while (1) {
        addrlen = sizeof client_addr;
        int num = recvfrom(sockfd, rec, MAXLINE - 1, 0,
                           (struct sockaddr *)&client_addr, &addrlen);
        if (num <= 0)
            continue;

        rec[--num] = '\0';

        inet_ntop(client_addr.ss_family,get_in_addr((struct sockaddr *)&client_addr),s, sizeof s);

        printf("Received from %s: %s\n", s, rec);

        count++;
        strcat(rec, " | Echo count : ");
        snprintf(sen, sizeof(sen), "%d", count);
        strcat(rec, sen);
        sendto(sockfd, rec, strlen(rec), 0,(struct sockaddr *)&client_addr, addrlen);        
    }

    close(sockfd);
    return 0;
}
