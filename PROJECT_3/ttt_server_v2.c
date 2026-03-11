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
#define MAX_GAMES 1000

int epfd;
struct epoll_event ev;

int handshake_done[10000]={0};
char usernames[10000][50]={{0}};
int registered[10000]={0};
char symbols[10000];
int client_game[10000];   

typedef struct {
    char board[9];
    char current_turn;
    int player_x_fd;
    int player_o_fd;
    int active;
} GameSession;

GameSession games[MAX_GAMES];

int make_socket_non_blocking(int fd);
void handle_new_connection(int sockfd);
void handle_incoming_request(int client_fd);
void make_handshake(int client_fd,unsigned char *buf);
void decode_header(int client_fd,unsigned char*buf,int* opcode,int* mask,int *mask_offset,unsigned long* payload_len);
void decode_message(unsigned char* buf,int mask_offset,int payload_len,char * decoded);
void register_client(int client_fd,char* decoded);
void process_move(int client_fd,char* decoded);
void generate_websocket_accept_key(const char *client_key,char *accept_key);
void send_text_frame(int client_fd,const char *msg);


int find_waiting_game()
{
    for(int i=0;i<MAX_GAMES;i++)
        if(games[i].active && games[i].player_o_fd==-1)
            return i;
    return -1;
}

int create_game()
{
    for(int i=0;i<MAX_GAMES;i++)
    {
        if(!games[i].active)
        {
            games[i].active=1;
            games[i].player_x_fd=-1;
            games[i].player_o_fd=-1;
            games[i].current_turn='X';
            for(int j=0;j<9;j++) games[i].board[j]=' ';
            return i;
        }
    }
    return -1;
}

int check_win(GameSession *g,char s)
{
    int w[8][3]={
        {0,1,2},{3,4,5},{6,7,8},
        {0,3,6},{1,4,7},{2,5,8},
        {0,4,8},{2,4,6}
    };

    for(int i=0;i<8;i++)
        if(g->board[w[i][0]]==s &&
           g->board[w[i][1]]==s &&
           g->board[w[i][2]]==s)
            return 1;

    return 0;
}

int check_draw(GameSession *g)
{
    for(int i=0;i<9;i++)
        if(g->board[i]==' ') return 0;
    return 1;
}

int make_socket_non_blocking(int fd)
{
    int flags=fcntl(fd,F_GETFL,0);
    if(flags==-1) return -1;
    return fcntl(fd,F_SETFL,flags|O_NONBLOCK);
}

void handle_new_connection(int sockfd)
{
    while(1)
    {
        struct sockaddr_storage addr;
        socklen_t len=sizeof addr;

        int client_fd=accept(sockfd,(struct sockaddr*)&addr,&len);

        if(client_fd==-1){
            if(errno==EAGAIN||errno==EWOULDBLOCK) break;
            else { perror("accept"); break; }
        }

        make_socket_non_blocking(client_fd);

        ev.events=EPOLLIN;
        ev.data.fd=client_fd;
        epoll_ctl(epfd,EPOLL_CTL_ADD,client_fd,&ev);

        handshake_done[client_fd]=0;
        client_game[client_fd]=-1;

        printf("New client connected: %d\n",client_fd);
    }
}

void cleanup_client(int fd)
{
    int gid = client_game[fd];

    if(gid >= 0)
    {
        GameSession *g = &games[gid];

        int opponent_fd = -1;

        if(g->player_x_fd == fd)
            opponent_fd = g->player_o_fd;

        else if(g->player_o_fd == fd)
            opponent_fd = g->player_x_fd;

        if(opponent_fd != -1)
        {
            send_text_frame(opponent_fd,"OPPONENT_LEFT");
        }

        if(g->player_x_fd == fd)
            g->player_x_fd = -1;

        if(g->player_o_fd == fd)
            g->player_o_fd = -1;

        g->active = 0;
    }

    client_game[fd] = -1;
    registered[fd] = 0;
    handshake_done[fd] = 0;
    symbols[fd] = 0;
    usernames[fd][0] = '\0';

    printf("Client %d cleaned\n", fd);
}

void handle_incoming_request(int client_fd)
{
    unsigned char buf[BUF_SIZE];
    int n=recv(client_fd,buf,BUF_SIZE,0);

    if(n<=0){
        epoll_ctl(epfd,EPOLL_CTL_DEL,client_fd,NULL);
        cleanup_client(client_fd);
        close(client_fd);
        return;
    }

    if(!handshake_done[client_fd])
        make_handshake(client_fd,buf);
    else
    {
        int opcode,mask,mask_offset;
        unsigned long payload_len;

        decode_header(client_fd,buf,&opcode,&mask,&mask_offset,&payload_len);

        if(opcode==0x8){
            epoll_ctl(epfd,EPOLL_CTL_DEL,client_fd,NULL);
            cleanup_client(client_fd);
            close(client_fd);
            return;
        }

        if(opcode==0x1 && mask==1)
        {
            char decoded[BUF_SIZE];
            decode_message(buf,mask_offset,payload_len,decoded);

            if(!registered[client_fd])
            {
                if(strncmp(decoded,"REGISTER:",9)==0)
                    register_client(client_fd,decoded);
            }
            else if(strncmp(decoded,"MOVE:",5)==0)
                process_move(client_fd,decoded);
        }
    }
}

void register_client(int client_fd,char* decoded)
{
    strncpy(usernames[client_fd],decoded+9,49);
    registered[client_fd]=1;

    int gid=find_waiting_game();

    if(gid==-1)
        gid=create_game();

    GameSession *g=&games[gid];
    client_game[client_fd]=gid;

    if(g->player_x_fd==-1)
    {
        g->player_x_fd=client_fd;
        symbols[client_fd]='X';
        send_text_frame(client_fd,"X");
    }
    else
    {
        g->player_o_fd=client_fd;
        symbols[client_fd]='O';
        send_text_frame(client_fd,"O");

        send_text_frame(g->player_x_fd,"YOUR_TURN");
        send_text_frame(g->player_o_fd,"WAIT");
    }
}

void process_move(int client_fd,char *decoded)
{
    int gid=client_game[client_fd];
    if(gid<0) return;

    GameSession *g=&games[gid];

    int cell;
    sscanf(decoded,"MOVE:%d",&cell);

    if(cell<0||cell>8) return;

    char sym=symbols[client_fd];

    if(sym!=g->current_turn){
        send_text_frame(client_fd,"INVALID");
        return;
    }

    if(g->board[cell]!=' '){
        send_text_frame(client_fd,"INVALID");
        return;
    }

    g->board[cell]=sym;

    char msg[32];
    sprintf(msg,"MOVE:%d:%c",cell,sym);

    send_text_frame(g->player_x_fd,msg);
    send_text_frame(g->player_o_fd,msg);

    if(check_win(g,sym))
    {
        if(sym=='X'){
            send_text_frame(g->player_x_fd,"WIN");
            send_text_frame(g->player_o_fd,"LOSE");
        }else{
            send_text_frame(g->player_o_fd,"WIN");
            send_text_frame(g->player_x_fd,"LOSE");
        }
        g->active=0;
        return;
    }

    if(check_draw(g))
    {
        send_text_frame(g->player_x_fd,"DRAW");
        send_text_frame(g->player_o_fd,"DRAW");
        g->active=0;
        return;
    }

    g->current_turn=(g->current_turn=='X')?'O':'X';

    if(g->current_turn=='X'){
        send_text_frame(g->player_x_fd,"YOUR_TURN");
        send_text_frame(g->player_o_fd,"WAIT");
    }else{
        send_text_frame(g->player_o_fd,"YOUR_TURN");
        send_text_frame(g->player_x_fd,"WAIT");
    }
}

void make_handshake(int client_fd,unsigned char *buf)
{
    if(strstr((char*)buf,"Upgrade: websocket"))
    {
        char *key_ptr=strstr((char*)buf,"Sec-WebSocket-Key:");
        key_ptr+=19;

        char client_key[256]={0};
        sscanf(key_ptr,"%255[^\r\n]",client_key);

        char accept_key[128];
        generate_websocket_accept_key(client_key,accept_key);

        char resp[512];
        snprintf(resp,sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_key);

        send(client_fd,resp,strlen(resp),0);
        handshake_done[client_fd]=1;
    }
}

void decode_header(int client_fd,unsigned char*buf,int* opcode,int* mask,int *mask_offset,unsigned long* payload_len)
{
    *opcode=buf[0]&0x0F;
    *mask=(buf[1]>>7)&1;
    *payload_len=buf[1]&0x7F;
    *mask_offset=2;
}

void decode_message(unsigned char* buf,int mask_offset,int payload_len,char * decoded)
{
    unsigned char masking_key[4];
    memcpy(masking_key,&buf[mask_offset],4);

    unsigned char *payload=&buf[mask_offset+4];

    for(unsigned long j=0;j<payload_len;j++)
        decoded[j]=payload[j]^masking_key[j%4];

    decoded[payload_len]='\0';
}

void generate_websocket_accept_key(const char *client_key,char *accept_key)
{
    const char *magic="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[256];
    unsigned char sha1_result[SHA_DIGEST_LENGTH];

    snprintf(combined,sizeof(combined),"%s%s",client_key,magic);
    SHA1((unsigned char*)combined,strlen(combined),sha1_result);
    EVP_EncodeBlock((unsigned char*)accept_key,sha1_result,SHA_DIGEST_LENGTH);
}

void send_text_frame(int client_fd,const char *msg)
{
    unsigned char frame[BUF_SIZE];
    size_t len=strlen(msg);

    frame[0]=0x81;
    frame[1]=len;
    memcpy(&frame[2],msg,len);
    send(client_fd,frame,len+2,0);
}

int main()
{
    int sockfd,yes=1,rv;
    struct addrinfo hints,*res,*p;
    struct epoll_event events[MAX_EVENTS];

    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_PASSIVE;

    getaddrinfo(NULL,PORT,&hints,&res);

    for(p=res;p;p=p->ai_next){
        sockfd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);
        setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int));
        if(bind(sockfd,p->ai_addr,p->ai_addrlen)==0) break;
    }

    listen(sockfd,BACKLOG);
    make_socket_non_blocking(sockfd);

    epfd=epoll_create1(0);

    ev.events=EPOLLIN;
    ev.data.fd=sockfd;
    epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);

    printf("Server running on %s\n",PORT);

    while(1)
    {
        int nfds=epoll_wait(epfd,events,MAX_EVENTS,-1);

        for(int i=0;i<nfds;i++)
        {
            if(events[i].data.fd==sockfd)
                handle_new_connection(sockfd);
            else
                handle_incoming_request(events[i].data.fd);
        }
    }
}