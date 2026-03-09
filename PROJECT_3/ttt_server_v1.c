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

int epfd;
struct epoll_event ev;

int handshake_done[10000] = {0};
char usernames[10000][50] = {{0}};
int registered[10000] = {0};
char board[9] = {' ',' ',' ',' ',' ',' ',' ',' ',' '};
char current_turn = 'X';
int player_x_fd = -1;
int player_o_fd = -1;

int player_count = 0;
char symbols[10000];

int make_socket_non_blocking(int fd);
void handle_new_connection(int sockfd);
void handle_incoming_request(int client_fd);
void make_handshake(int client_fd, unsigned char *buf);
void decode_header(int client_fd,unsigned char*buf,int* opcode,int* mask,int *mask_offset,unsigned long* payload_len);
void decode_message(unsigned char* buf,int mask_offset,int payload_len,char * decoded);
void register_client(int client_fd, char* decoded);
void generate_websocket_accept_key(const char *client_key,char *accept_key);
void send_text_frame(int client_fd, const char *msg);
void process_move(int client_fd,char * decoded);
int check_win(char symbol);
int check_draw();

int make_socket_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void handle_new_connection(int sockfd)
{
    while (1)
    {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof client_addr;

        int client_fd = accept(sockfd,(struct sockaddr *)&client_addr,&addr_len);

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

void handle_incoming_request(int client_fd)
{
    unsigned char buf[BUF_SIZE];

    int n = recv(client_fd, buf, BUF_SIZE, 0);

    if (n <= 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, NULL);
        handshake_done[client_fd] = 0;
        registered[client_fd] = 0;
        close(client_fd);
        return;
    }

    if (!handshake_done[client_fd])
    {
        make_handshake(client_fd, buf);
    }
    else
    {
        int opcode, mask, mask_offset;
        unsigned long payload_len;

        decode_header(client_fd,buf,&opcode,&mask,&mask_offset,&payload_len);

        if (opcode == 0x8) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, NULL);
            handshake_done[client_fd] = 0;
            registered[client_fd] = 0;
            close(client_fd);
            return;
        }

        if (opcode == 0x1 && mask == 1) {

            char decoded[BUF_SIZE];

            decode_message(buf,mask_offset,payload_len,decoded);

            if (!registered[client_fd]) {
                if (strncmp(decoded,"REGISTER:",9)==0){
                    register_client(client_fd,decoded);
                }
            }
            else
            {
                if(strncmp(decoded,"MOVE:",5)==0)
                {
                    process_move(client_fd,decoded);
                }
            }
        }
    }
}

void make_handshake(int client_fd, unsigned char *buf)
{
    if (strstr((char *)buf,"Connection: Upgrade") &&
        strstr((char *)buf,"Upgrade: websocket"))
    {
        char *key_ptr =
            strstr((char *)buf,"Sec-WebSocket-Key:");

        if (!key_ptr) return;

        key_ptr += 19;

        printf("HANDSHAKE REQUEST : \n%s",(char *)buf);

        char client_key[256] = {0};
        sscanf(key_ptr,"%255[^\r\n]",client_key);

        char accept_key[128];
        generate_websocket_accept_key(client_key,accept_key);

        char resp[512];
        snprintf(resp,sizeof(resp),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n"
            "\r\n",
            accept_key);

        send(client_fd,resp,strlen(resp),0);

        printf("Handshake completed: %d\n\n",client_fd);

        handshake_done[client_fd]=1;
    }
}

void decode_header(int client_fd,unsigned char*buf,int* opcode,int* mask,int *mask_offset,unsigned long* payload_len)
{
    unsigned char byte1 = buf[0];
    unsigned char byte2 = buf[1];

    *opcode = byte1 & 0x0F;
    *mask = (byte2 >> 7) & 1;
    *payload_len = byte2 & 0x7F;

    if (*payload_len > MAX_ALLOWED) {
        send_text_frame(client_fd,"Message limit exceeded");
        return;
    }

    if (*payload_len == 126) {
        *payload_len = (buf[2] << 8) | buf[3];
        *mask_offset = 4;
    }
    else if (*payload_len == 127) {
        return;
    }
    else {
        *mask_offset = 2;
    }
}

void decode_message(unsigned char* buf,int mask_offset,int payload_len,char * decoded)
{
    unsigned char masking_key[4];
    memcpy(masking_key,&buf[mask_offset],4);

    unsigned char *payload=&buf[mask_offset+4];

    for (unsigned long j=0;j<payload_len;j++)
        decoded[j]=payload[j]^masking_key[j%4];

    decoded[payload_len]='\0';

    printf("WebSocket message received : \n%s\n\n",decoded);
}

void register_client(int client_fd,char* decoded)
{
    char new_username[50];

    strncpy(new_username,decoded+9,49);
    new_username[49]='\0';

    strncpy(usernames[client_fd],new_username,49);

    registered[client_fd]=1;

    printf("User registered: %s (fd=%d)\n",
           usernames[client_fd],client_fd);

    if(player_count==0)
    {
        symbols[client_fd]='X';
        player_x_fd = client_fd;

        player_count++;

        send_text_frame(client_fd,"X");

        printf("%s assigned symbol X\n\n",usernames[client_fd]);
    }
    else if(player_count==1)
    {
        symbols[client_fd]='O';
        player_o_fd = client_fd;

        player_count++;

        send_text_frame(client_fd,"O");

        printf("%s assigned symbol O\n\n",usernames[client_fd]);

        for(int i=0;i<9;i++)
            board[i]=' ';

        current_turn='X';

        send_text_frame(player_x_fd,"YOUR_TURN");
        send_text_frame(player_o_fd,"WAIT");
    }
    else
    {
        send_text_frame(client_fd,"MAX");

        printf("Extra player rejected\n\n");

        registered[client_fd]=0;
    }
}

int check_win(char symbol)
{
    int wins[8][3] = {
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };

    for(int i=0;i<8;i++)
    {
        if(board[wins[i][0]]==symbol &&
           board[wins[i][1]]==symbol &&
           board[wins[i][2]]==symbol)
        {
            return 1;
        }
    }

    return 0;
}

int check_draw()
{
    for(int i=0;i<9;i++)
    {
        if(board[i]==' ')
            return 0;
    }

    return 1;
}

void process_move(int client_fd, char *decoded)
{
    int cell;

    sscanf(decoded,"MOVE:%d",&cell);

    if(cell <0 || cell >8)
        return;

    char player_symbol = symbols[client_fd];

    if(player_symbol != current_turn)
    {
        send_text_frame(client_fd,"INVALID");
        return;
    }

    if(board[cell] != ' ')
    {
        send_text_frame(client_fd,"INVALID");
        return;
    }

    board[cell] = player_symbol;

    char msg[50];
    sprintf(msg,"MOVE:%d:%c",cell,player_symbol);

    if(player_x_fd!=-1)
        send_text_frame(player_x_fd,msg);

    if(player_o_fd!=-1)
        send_text_frame(player_o_fd,msg);

    if(check_win(player_symbol))
    {
        if(player_symbol=='X')
        {
            send_text_frame(player_x_fd,"WIN");
            send_text_frame(player_o_fd,"LOSE");
        }
        else
        {
            send_text_frame(player_o_fd,"WIN");
            send_text_frame(player_x_fd,"LOSE");
        }

        return;
    }

    if(check_draw())
    {
        send_text_frame(player_x_fd,"DRAW");
        send_text_frame(player_o_fd,"DRAW");
        return;
    }

    if(current_turn=='X')
        current_turn='O';
    else
        current_turn='X';

    if(current_turn=='X')
    {
        send_text_frame(player_x_fd,"YOUR_TURN");
        send_text_frame(player_o_fd,"WAIT");
    }
    else
    {
        send_text_frame(player_o_fd,"YOUR_TURN");
        send_text_frame(player_x_fd,"WAIT");
    }
}

void generate_websocket_accept_key(const char *client_key,char *accept_key)
{
    const char *magic_string ="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    char combined[256];
    unsigned char sha1_result[SHA_DIGEST_LENGTH];

    snprintf(combined,sizeof(combined),"%s%s",client_key,magic_string);

    SHA1((unsigned char *)combined,strlen(combined),sha1_result);

    EVP_EncodeBlock((unsigned char *)accept_key,sha1_result,SHA_DIGEST_LENGTH);
}

void send_text_frame(int client_fd,const char *msg)
{
    unsigned char frame[BUF_SIZE];
    size_t len=strlen(msg);

    frame[0]=0x81;

    if(len<=125){
        frame[1]=len;
        memcpy(&frame[2],msg,len);
        send(client_fd,frame,len+2,0);
    }
    else if(len<=65535){
        frame[1]=126;
        frame[2]=(len>>8)&0xFF;
        frame[3]=len&0xFF;
        memcpy(&frame[4],msg,len);
        send(client_fd,frame,len+4,0);
    }
}

int main(void)
{
    int sockfd, rv, yes=1;
    struct addrinfo hints,*res,*p;
    struct epoll_event events[MAX_EVENTS];

    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_PASSIVE;

    if((rv=getaddrinfo(NULL,PORT,&hints,&res))!=0){
        fprintf(stderr,"getaddrinfo: %s\n",gai_strerror(rv));
        return 1;
    }

    for(p=res;p!=NULL;p=p->ai_next){
        sockfd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);
        if(sockfd==-1) continue;

        setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int));

        if(bind(sockfd,p->ai_addr,p->ai_addrlen)==-1){
            close(sockfd);
            continue;
        }
        break;
    }

    listen(sockfd,BACKLOG);
    make_socket_non_blocking(sockfd);

    epfd=epoll_create1(0);

    ev.events=EPOLLIN;
    ev.data.fd=sockfd;
    epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);

    printf("WebSocket server running on port %s\n",PORT);

    while(1){
        int nfds=epoll_wait(epfd,events,MAX_EVENTS,-1);

        for(int i=0;i<nfds;i++){
            if(events[i].data.fd==sockfd)
            {
                handle_new_connection(sockfd);
            }
            else
            {
                handle_incoming_request(events[i].data.fd);
            }
        }
    }

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}