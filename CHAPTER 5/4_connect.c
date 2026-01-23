#include<stdio.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<unistd.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>

#define PORT "3490"
#define IP "::1"

int main(){
    struct addrinfo hints,*res,*p;
    int status;
    int sockfd;

    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_INET6;
    hints.ai_socktype=SOCK_STREAM;

    if((status=getaddrinfo(IP,PORT,&hints,&res))!=0){
        fprintf(stderr,"error: %s",gai_strerror(status));
        return 1;
    }

    for(p=res;p!=NULL;p=p->ai_next){
        sockfd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);
        if(sockfd!=-1)
            break;
    }

    if(p==NULL){
        printf("Error : socket not created");
        freeaddrinfo(res);
        return 2;
    }

    if(connect(sockfd,p->ai_addr,p->ai_addrlen)!=-1)
        printf("Connected sucessfully\n");
    else
        perror("connect");
    
    freeaddrinfo(res);
    close(sockfd);
    return 0;
    
}
