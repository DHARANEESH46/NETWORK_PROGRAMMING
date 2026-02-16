#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PORT "3490"
#define BUFFER_SIZE 4096


int connect_to_server(const char *ip) {

    struct addrinfo hints, *res;
    int sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip, PORT, &hints, &res) != 0) {
        perror("getaddrinfo");
        exit(1);
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect");
        exit(1);
    }

    freeaddrinfo(res);
    return sockfd;
}


void receive_response(int sockfd) {

    char buffer[BUFFER_SIZE];
    int n;

    printf("\n----- SERVER RESPONSE -----\n\n");

    while ((n = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }
}


int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    int sockfd = connect_to_server(argv[1]);

    char method[10];
    printf("Enter method (GET/POST): ");
    scanf("%9s", method);

    //GET
    if (strcasecmp(method, "GET") == 0) {

        char path[256];
        printf("Enter file/path (example: / or /index.html): ");
        scanf("%255s", path);

        char request[1024];

        snprintf(request, sizeof(request),
                 "GET %s HTTP/1.0\r\n"
                 "Host: %s:3490\r\n"
                 "User-Agent: curl/8.5.0\r\n"
                 "Accept: */*\r\n"
                 "\r\n",
                 path, argv[1]);

        send(sockfd, request, strlen(request), 0);
        receive_response(sockfd);
    }

    //POST
    else if (strcasecmp(method, "POST") == 0) {

        char content_type[20];
        printf("Enter content type (text/file): ");
        scanf("%19s", content_type);

        //TEXT
        if (strcasecmp(content_type, "text") == 0) {

            getchar(); // consume newline

            char body[2048];
            printf("Enter text message: ");
            fgets(body, sizeof(body), stdin);

            body[strcspn(body, "\n")] = 0; // remove newline
            int len = strlen(body);

            char request[4096];

            snprintf(request, sizeof(request),
                     "POST /upload HTTP/1.0\r\n"
                     "Host: %s:3490\r\n"
                     "User-Agent: curl/8.5.0"
                     "Accept: */*"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %d\r\n"
                     "\r\n"
                     "%s",
                     argv[1], len, body);

            send(sockfd, request, strlen(request), 0);
            receive_response(sockfd);
        }

        //MULTIPART
        else if (strcasecmp(content_type, "file") == 0) {

            char filepath[512];
            printf("Enter file path: ");
            scanf("%511s", filepath);

            int fd = open(filepath, O_RDONLY);
            if (fd < 0) {
                perror("open file");
                close(sockfd);
                return 1;
            }

            struct stat st;
            fstat(fd, &st);
            long filesize = st.st_size;

            char *filedata = malloc(filesize);
            read(fd, filedata, filesize);
            close(fd);

            char *filename = strrchr(filepath, '/');
            filename = filename ? filename + 1 : filepath;

            char boundary[] = "----MYBOUNDARY12345";

            char header[4096];

            int body_len = snprintf(NULL, 0,
                "--%s\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                "Content-Type: application/octet-stream\r\n"
                "\r\n",
                boundary, filename)
                + filesize
                + strlen("\r\n--") + strlen(boundary) + strlen("--\r\n");

            char *body = malloc(body_len + 1);

            int offset = sprintf(body,
                "--%s\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                "Content-Type: application/octet-stream\r\n"
                "\r\n",
                boundary, filename);

            memcpy(body + offset, filedata, filesize);
            offset += filesize;

            offset += sprintf(body + offset,
                "\r\n--%s--\r\n", boundary);

            snprintf(header, sizeof(header),
                    "POST /upload HTTP/1.1\r\n"
                    "Host: localhost:3490\r\n"
                    "User-Agent: curl/8.5.0\r\n"
                    "Accept: */*\r\n"
                    "Content-Type: multipart/form-data; boundary=%s\r\n"
                    "Content-Length: %d\r\n"
                    "\r\n",
                    boundary, offset);


            send(sockfd, header, strlen(header), 0);
            send(sockfd, body, offset, 0);

            receive_response(sockfd);

            free(filedata);
            free(body);
        }

        else {
            printf("Invalid content type\n");
        }
    }

    else {
        printf("Invalid method\n");
    }

    close(sockfd);
    return 0;
}
