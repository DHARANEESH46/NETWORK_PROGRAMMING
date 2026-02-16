#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <sys/stat.h>

#define PORT "3490"
#define MAX_EVENTS 1000
#define BUF_SIZE 1024
#define BACKLOG 100

int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

const char *get_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".jpg")  == 0) return "image/jpeg";
    if (strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".pdf")  == 0) return "application/pdf";

    return "application/octet-stream";
}

int main(void)
{
    int sockfd, epfd, rv, yes = 1;
    struct addrinfo hints, *res, *p;
    struct epoll_event ev, events[MAX_EVENTS];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        printf("failed to bind\n");
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    make_socket_non_blocking(sockfd);

    epfd = epoll_create1(0);
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    printf("Epoll HTTP server running on port %s\n", PORT);

    while (1) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);

        for (int i = 0; i < nfds; i++) {

            if (events[i].data.fd == sockfd) {

                while (1) {
                    struct sockaddr_storage client_addr;
                    socklen_t addr_len = sizeof client_addr;

                    int client_fd = accept(sockfd,
                                           (struct sockaddr *)&client_addr,
                                           &addr_len);
                    if (client_fd == -1)
                        break;

                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            }
            else {

                int client_fd = events[i].data.fd;
                char buf[BUF_SIZE];

                int n = recv(client_fd, buf, BUF_SIZE - 1, 0);
                if (n <= 0) {
                    close(client_fd);
                    continue;
                }

                buf[n] = '\0';
                buf[n] = '\0';

                char *header_end = strstr(buf, "\r\n\r\n"); //pointer to header ending
                if (header_end) {
                    int header_len = header_end - buf; //length of header portion
                    printf("Received:\n%.*s\n\n", header_len, buf);
                } 


                char method[8], path[256];
                sscanf(buf, "%7s %255s", method, path);
                
                //GET
                if (strcmp(method, "GET") == 0) {

                    if (strcmp(path, "/") == 0)
                        strcpy(path, "/index.html");

                    char file_path[512];
                    snprintf(file_path, sizeof(file_path), "/var/www%s", path);

                    int fd = open(file_path, O_RDONLY);
                    if (fd == -1) {
                        const char *body = "File Not Found";
                        char resp[256];

                        snprintf(resp, sizeof(resp),
                            "HTTP/1.0 404 Not Found\r\n"
                            "Content-Length: %ld\r\n"
                            "Content-Type: text/plain\r\n\r\n%s",
                            strlen(body), body);

                        send(client_fd, resp, strlen(resp), 0);
                        close(client_fd);
                        continue;
                    }

                    struct stat st;
                    fstat(fd, &st);

                    char header[256];
                    snprintf(header, sizeof(header),
                        "HTTP/1.0 200 OK\r\n"
                        "Content-Length: %ld\r\n"
                        "Content-Type: %s\r\n\r\n",
                        st.st_size, get_type(file_path));

                    send(client_fd, header, strlen(header), 0);

                    char file_buf[4096];
                    int r;
                    while ((r = read(fd, file_buf, sizeof(file_buf))) > 0)
                        send(client_fd, file_buf, r, 0);

                    close(fd);
                    close(client_fd);
                }

                //POST
                else if (strcmp(method, "POST") == 0) {

                    long long content_length = 0;   
                    char *cl = strstr(buf, "Content-Length:");//pointer to content length
                    if (cl)
                        sscanf(cl, "Content-Length: %lld", &content_length);  

                    char *header_end = strstr(buf, "\r\n\r\n");
                    if (!header_end) {
                        close(client_fd);
                        continue;
                    }

                    header_end += 4;

                    int header_size = header_end - buf;
                    long long body_read = n - header_size; //how many bodu bytes read in first recv

                    char *body = malloc(content_length);    
                    if (!body) {
                        perror("malloc failed");            
                        close(client_fd);
                        continue;
                    }

                    memcpy(body, header_end, body_read);

                    long long remaining = content_length - body_read; //remaining body need to be read
                    long long offset = body_read;                      

                    while (remaining > 0) {
                        int r = recv(client_fd, body + offset, remaining, 0);
                        if (r <= 0) break;
                        offset += r;
                        remaining -= r;
                    }

                    printf("FULL BODY RECEIVED (%lld bytes)\n", offset); 

                    char *ctype = strstr(buf, "Content-Type:");//pointer to content type

                    //MULTIPART
                    if (ctype && strstr(ctype, "multipart/form-data")) {

                        char *bptr = strstr(ctype, "boundary=");//pointer to boundary
                        if (bptr) {

                            bptr += 9;
                            char boundary[256] = {0};
                            sscanf(bptr, "%255s", boundary);

                            char full_boundary[300];
                            snprintf(full_boundary, sizeof(full_boundary),
                                    "--%s", boundary);

                            char *file_part = memmem(body, offset, "filename=\"", 10);

                            if (file_part) {

                                file_part += 10;
                                char filename[256] = {0};
                                sscanf(file_part, "%255[^\"]", filename);

                                printf("Filename: %s\n", filename);

                                char *data_start = memmem(file_part,
                                                        body + offset - file_part,
                                                        "\r\n\r\n", 4);
                                if (data_start) {

                                    data_start += 4;

                                    char end_boundary[512];
                                    snprintf(end_boundary, sizeof(end_boundary),
                                            "\r\n--%s--", boundary);

                                    char *data_end = memmem(data_start,
                                                            body + offset - data_start,
                                                            end_boundary,
                                                            strlen(end_boundary));

                                    if (data_end) {

                                        long long file_size = data_end - data_start;  

                                        char save_path[512];
                                        snprintf(save_path, sizeof(save_path),
                                                "/var/www/uploads/%s", filename);

                                        int out = open(save_path,
                                                    O_WRONLY | O_CREAT | O_TRUNC,
                                                    0644);

                                        if (out < 0) {
                                            perror("open upload file");
                                        } else{
                                            long long written = 0;
                                            while (written < file_size) {
                                                ssize_t w = write(out,
                                                                data_start + written,
                                                                file_size - written);
                                                if (w <= 0) break;
                                                written += w;
                                            }
                                            close(out);
                                            printf("File saved: %s (%lld bytes)\n\n",
                                                save_path, file_size);  
                                        }
                                    }
                                }
                            }
                        }

                        const char *resp_body = "Multipart data processed successfully";

                        char resp[256];
                        snprintf(resp, sizeof(resp),
                            "HTTP/1.0 200 OK\r\n"
                            "Content-Length: %ld\r\n"
                            "Content-Type: text/plain\r\n\r\n%s",
                            strlen(resp_body), resp_body);

                        send(client_fd, resp, strlen(resp), 0);
                    }

                    //NORMAL TEXT
                    else {

                        printf("POST TEXT DATA:\n%.*s\n\n", (int)offset, body);  

                        const char *resp_body = "POST data received successfully";

                        char resp[256];
                        snprintf(resp, sizeof(resp),
                            "HTTP/1.0 200 OK\r\n"
                            "Content-Length: %ld\r\n"
                            "Content-Type: text/plain\r\n\r\n%s",
                            strlen(resp_body), resp_body);

                        send(client_fd, resp, strlen(resp), 0);
                    }

                    free(body);
                    close(client_fd);
                }



                //OTHER METHODS
                else {
                    const char *body = "Method Not Allowed";
                    char resp[256];

                    snprintf(resp, sizeof(resp),
                        "HTTP/1.0 405 Method Not Allowed\r\n"
                        "Content-Length: %ld\r\n"
                        "Content-Type: text/plain\r\n\r\n%s",
                        strlen(body), body);

                    send(client_fd, resp, strlen(resp), 0);
                    close(client_fd);
                }
            }
        }
    }

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}

