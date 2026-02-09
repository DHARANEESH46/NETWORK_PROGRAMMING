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
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "3490"
#define BACKLOG 10
#define MAXLINE 10000

void sigchld_handler(int s)
{
    (void)s;

    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

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
    int newfd;
    struct sockaddr_storage addr;
    struct sigaction sa;
    char rec[MAXLINE];
    char sen[MAXLINE];
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int count = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "error: %s\n", gai_strerror(status));
        return 1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            close(sockfd);
            freeaddrinfo(res);
            return 1;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("bind");
            close(sockfd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "failed to bind\n");
        freeaddrinfo(res);
        return 1;
    }
    
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    printf("waiting for connection...\n");

    while (1) {
        socklen_t addrlen = sizeof(addr);
        newfd = accept(sockfd, (struct sockaddr *)&addr, &addrlen);
        if (newfd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(addr.ss_family, get_in_addr((struct sockaddr *)&addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (!fork()) {
            close(sockfd);

            while (1) {
                int num = recv(newfd, rec, MAXLINE - 1, 0);
                if (num <= 0)
                    break;

                rec[--num] = '\0';
                count++;
                strcat(rec, " | Echo count : ");
                snprintf(sen, sizeof(sen), "%d", count);
                strcat(rec, sen);
                send(newfd, rec, strlen(rec), 0);
            }

            printf("Client %s disconnected\n", s);
            close(newfd);
            exit(0);
        }

        close(newfd);
    }

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}
