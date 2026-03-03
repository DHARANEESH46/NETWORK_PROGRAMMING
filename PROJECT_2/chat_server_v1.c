#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#define PORT "8080"
#define MAX_EVENTS 1000
#define BUF_SIZE 9000
#define BACKLOG 100
#define MAX_ALLOWED 8000

int handshake_done[10000] = {0};

char usernames[10000][50] = {{0}};
int registered[10000] = {0};

int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void generate_websocket_accept_key(const char *client_key, char *accept_key)
{
    const char *magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[256];
    unsigned char sha1_result[SHA_DIGEST_LENGTH];

    snprintf(combined, sizeof(combined), "%s%s", client_key, magic_string);
    SHA1((unsigned char *)combined, strlen(combined), sha1_result);
    EVP_EncodeBlock((unsigned char *)accept_key,
                    sha1_result,
                    SHA_DIGEST_LENGTH);
}

void send_text_frame(int client_fd, const char *msg)
{
    unsigned char frame[BUF_SIZE];
    size_t len = strlen(msg);

    frame[0] = 0x81; // 1000 0001

    if (len <= 125) {
        frame[1] = len;
        memcpy(&frame[2], msg, len);
        send(client_fd, frame, len + 2, 0);
    }
    else if (len <= 65535) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;   //MSB
        frame[3] = len & 0xFF;          //LSB
        memcpy(&frame[4], msg, len);
        send(client_fd, frame, len + 4, 0);
    }
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
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;

        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        printf("Failed to bind\n");
        return 1;
    }

    listen(sockfd, BACKLOG);
    make_socket_non_blocking(sockfd);

    epfd = epoll_create1(0);

    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    printf("WebSocket server running on port %s\n", PORT);

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

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        else {
                            perror("accept");
                            break;
                        }
                    }

                    make_socket_non_blocking(client_fd);

                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);

                    handshake_done[client_fd] = 0;

                    printf("New client connected: %d\n\n", client_fd);
                }
            }
            else {

                int client_fd = events[i].data.fd;
                unsigned char buf[BUF_SIZE];

                int n = recv(client_fd, buf, BUF_SIZE, 0);

                if (n <= 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, NULL);
                    handshake_done[client_fd] = 0;
                    registered[client_fd] = 0;
                    close(client_fd);
                    continue;
                }
                
                if (!handshake_done[client_fd]) 
                {
                    if (strstr((char *)buf,"Connection: Upgrade") &&
                        strstr((char *)buf,"Upgrade: websocket"))
                    {
                        char *key_ptr = strstr((char *)buf,"Sec-WebSocket-Key:");
                        if (!key_ptr) continue;

                        key_ptr += 19;

                        printf("HANDSHAKE REQUEST : \n%s",(char *)buf);

                        char client_key[256] = {0};
                        sscanf(key_ptr, "%255[^\r\n]", client_key);

                        char accept_key[128];
                        generate_websocket_accept_key(client_key, accept_key);

                        char resp[512];
                        snprintf(resp, sizeof(resp),
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: %s\r\n"
                            "\r\n",
                            accept_key);

                        send(client_fd, resp, strlen(resp), 0);

                        printf("Handshake completed: %d\n\n", client_fd);

                        handshake_done[client_fd] = 1;
                    }
                }
                else 
                {
                    unsigned char byte1 = buf[0];
                    unsigned char byte2 = buf[1];

                    int opcode = byte1 & 0x0F;
                    int mask = (byte2 >> 7) & 1;
                    unsigned long payload_len = byte2 & 0x7F;

                    int mask_offset;

                    if (payload_len > MAX_ALLOWED) {
                        send_text_frame(client_fd, "Message limit exceeded");
                        continue;
                    }
                    if (payload_len == 126) {
                        payload_len = (buf[2] << 8) | buf[3];
                        mask_offset = 4;
                    }
                    else if (payload_len == 127) {
                        continue;
                    }
                    else {
                        mask_offset = 2;
                    }

                    if (opcode == 0x8) {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, NULL);
                        handshake_done[client_fd] = 0;
                        registered[client_fd] = 0;
                        close(client_fd);
                        continue;
                    }

                    if (opcode == 0x1 && mask == 1) {

                        unsigned char masking_key[4];
                        memcpy(masking_key, &buf[mask_offset], 4);

                        unsigned char *payload = &buf[mask_offset + 4];

                        char decoded[BUF_SIZE];
                        for (unsigned long j = 0; j < payload_len; j++) {
                            decoded[j] = payload[j] ^ masking_key[j % 4];
                        }
                        decoded[payload_len] = '\0';
                        printf("WebSocket message received : \n%s\n\n", decoded);
                        
                        if (!registered[client_fd]) {
                            if (strncmp(decoded, "REGISTER:", 9) == 0) {

                                char new_username[50];
                                strncpy(new_username, decoded + 9, 49);
                                new_username[49] = '\0';

                                int duplicate = 0;

                                for (int fd = 0; fd < 10000; fd++) {
                                    if (handshake_done[fd] == 1 &&
                                        registered[fd] == 1 &&
                                        strcmp(usernames[fd], new_username) == 0) {
                                        duplicate = 1;
                                        break;
                                    }
                                }

                                if (duplicate) {
                                    send_text_frame(client_fd, "Username already available");
                                } else {
                                    strncpy(usernames[client_fd], new_username, 49);
                                    usernames[client_fd][49] = '\0';
                                    registered[client_fd] = 1;
                                    send_text_frame(client_fd, "Registered successfully");
                                    printf("User registered: %s (fd=%d)\n\n",
                                        usernames[client_fd], client_fd);
                                }
                            }
                        }
                        else {
                            if (strncmp(decoded, "PUBLIC:", 7) == 0) {

                                char *actual_msg = decoded + 7;

                                char message_with_name[BUF_SIZE];

                                size_t max_msg_len = sizeof(message_with_name)
                                                    - strlen(usernames[client_fd])
                                                    - 3;

                                if (strlen(actual_msg) > max_msg_len)
                                    actual_msg[max_msg_len] = '\0';

                                snprintf(message_with_name,
                                        sizeof(message_with_name),
                                        "%s: %s",
                                        usernames[client_fd],
                                        actual_msg);

                                for (int fd = 0; fd < 10000; fd++) {
                                    if (handshake_done[fd] == 1 &&
                                        registered[fd] == 1 &&
                                        fd != client_fd) {
                                        send_text_frame(fd, message_with_name);
                                    }
                                }
                            }
                            else if (strncmp(decoded, "PRIVATE:", 8) == 0) {

                                char *receiver = decoded + 8;
                                char *colon = strchr(receiver, ':');

                                if (!colon) continue;

                                *colon = '\0';
                                char *actual_msg = colon + 1;

                                int receiver_fd = -1;

                                for (int fd = 0; fd < 10000; fd++) {
                                    if (handshake_done[fd] == 1 &&
                                        registered[fd] == 1 &&
                                        strcmp(usernames[fd], receiver) == 0) {
                                        receiver_fd = fd;
                                        break;
                                    }
                                }

                                if (receiver_fd == -1) {

                                    send_text_frame(client_fd, "User not available");

                                } else {

                                    char message_with_name[BUF_SIZE];

                                    size_t max_msg_len = sizeof(message_with_name)
                                                        - strlen(usernames[client_fd])
                                                        - 12;

                                    if (strlen(actual_msg) > max_msg_len)
                                        actual_msg[max_msg_len] = '\0';

                                    snprintf(message_with_name,
                                            sizeof(message_with_name),
                                            "[Private] %s: %s",
                                            usernames[client_fd],
                                            actual_msg);

                                    send_text_frame(receiver_fd, message_with_name);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}