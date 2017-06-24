#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <rpc/xdr.h>
#include <ctype.h>

#include "types.h"

#define BUFLEN 32
#define MAX_CHILD 10
#define BUFSIZE 256

#define GET_B   "GET "
#define QUIT_B  "QUIT\r\n"
#define OK_B    "+OK\r\n" 
#define ERR_B   "-ERR\r\n"


int handle_client( int socket );
int handle_client_xdr( int sockc );
int initTcpConnection(int port, int n);
int readn(int s, char *ptr, size_t len);
int writen(int s, char *ptr, size_t nbytes);
void freeSupportData(char*, char*, char*, char*, char*);

int main(int argc, char * argv[]){
    
    struct sockaddr_in client[MAX_CHILD];
    int s, sockc, pid[MAX_CHILD], i;
    socklen_t csize;
    int nchild, port;
    int ppid = getpid();
    int xdr_mode = 0;
    
    for(i=0; i<argc; i++){
        if( !strcmp(argv[i], "-x") )
            xdr_mode = 1;
    }
    
    if(xdr_mode){
        port = atoi(argv[2]);
        nchild = atoi(argv[3]);
    }else if(argc == 3){
        port = atoi(argv[1]);
        nchild = atoi(argv[2]);
    }else{
        printf("Format: <program_name> [<-x>] <port> <child_forked>\n");
        exit(-1);
    }
    
    if(nchild > MAX_CHILD){
        printf("Parameter 'child' is out of range\nFormat: <program_name> <port> <child_forked>\n");
        exit(0);
    }   
    
    
    s = initTcpConnection(port, nchild);
    
    csize = sizeof(client);
        
    for(i = 0; i < nchild; i++){
        pid[i] = fork();
        if( pid[i] == 0 )
            break;
    }
    
    if( getpid() != ppid ){
        
        while(1){
            sockc = accept(s, (struct sockaddr *) &client[0], &csize);
            if(xdr_mode)
                handle_client_xdr(sockc);
            else
                handle_client(sockc);
        }
        
    }else {
        int x;
        while ( ( x = waitpid(-1, NULL, 0) ) > 0){
            printf("nel while della waitpid (%d) %d\n", getpid(), x);
            
        }
    }
       
    return 0;
    
}

int initTcpConnection(int port, int n){
    
    int s, bklog = n;
    struct sockaddr_in server;
    
    // create socket
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("socket created\n");
    
    // binding socket
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    if( bind(s, (struct sockaddr *) &server, sizeof(server) ) < 0 ){
        printf("bind() failed\n");
        close(s);
        exit(-1);
    }
    
    // start listening for bklog max client in the queue
    if( listen(s, bklog) < 0 ){
        printf("Error during listen()\n");
        close(s);
        exit(-1);
    }else printf("listening on %s:%d\n", inet_ntoa(server.sin_addr), ntohs(server.sin_port) );
    
    return s;
    
}

int handle_client( int socket ){
    
    int  sockc, bytes_rec, bytes_sent, n;
    char *err, *ok, *quit, *buff, *filename;
    int fd;
    int *file_size, *ts;
    struct stat file_stat;
    
    sockc = socket;
    
    struct timeval tval;
    fd_set cset;
    
    int t = 120; tval.tv_usec = 0;
    
    err = (char*) malloc(6*sizeof(char));
    strcpy(err, "-ERR\r\n");
    ok = (char*) malloc(5*sizeof(char));
    strcpy(ok, "+OK\r\n");
    quit = (char*) malloc(6*sizeof(char));
    strcpy(quit, "QUIT\r\n");
    
    
    while(1){ 
        
        tval.tv_sec = t;        
        FD_ZERO(&cset); FD_SET(sockc, &cset);
        buff = (char*) malloc(BUFLEN*sizeof(char));
        if( ( n = select(FD_SETSIZE, &cset, NULL, NULL, &tval) ) == -1 ){
            printf("select() failed\n");
            printf("errno: %d\n", errno);
            close(sockc);
            freeSupportData(err, ok, quit, filename, buff); 
            return 0;
        }
        if (n>0){
            
            if( ( bytes_rec = recv(sockc, buff, BUFLEN, 0) ) < 0){
                printf("Error during receiving command from client\n");
                close(sockc);
                freeSupportData(err, ok, quit, filename, buff);
                return  0;
            } else if(bytes_rec == 0){
                printf("(%d)  client closed current connection\n", getpid()); 
                close(sockc);
                freeSupportData(err, ok, quit, filename, buff);
                return 0;
            } else {
                
                if( !strncmp(buff, quit, 6) ){                    
                    printf("(%d)  client wants to close connection\n", getpid());
                    close(sockc);
                    printf("(%d)  connection closed\n", getpid());
                    freeSupportData(err, ok, quit, filename, buff);
                    return 0;
                } 
            }
            
            filename = (char*) malloc((strlen(buff) - 4)*sizeof(char));
            sscanf(buff, "%*s %s", filename);      
            
            // open the file            
            fd = open(filename, O_RDONLY);
            if(fd < 0){
                // can't open file
                perror("prova perror: ");
                printf("(%d)  Error opening file %s\n", getpid(), filename);
                if( ( bytes_sent = writen(sockc, err, 6) ) < 0 ){
                    printf("(%d)  Error sending data\n", getpid());                    
                }
                close(sockc);
                freeSupportData(err, ok, quit, filename, buff);
                return  0;
                
            } else {
                // file opened correctly
                printf("(%d)  File '%s' opened\n", getpid(), filename);
                if( ( bytes_sent = writen(sockc, ok, 5) ) < 0){
                    printf("Error sending data\n");
                    return  0;
                }
            }
            
            // get stas from file
            if( fstat(fd, &file_stat) < 0 ){
                
                printf("(%d)  Error getting file stats.\n", getpid());
                if( ( bytes_sent = writen(sockc, err, sizeof(err)) ) < 0 ){
                    printf("(%d)  Error sending data\n", getpid());
                    close(sockc);                    
                } 
                freeSupportData(err, ok, quit, filename, buff);
                return  0;
                
            } else {
                
                // sending file size
                printf("(%d)  File size: %ld\n", getpid(), file_stat.st_size);
                file_size = (int*) malloc(4*sizeof(char));
                *file_size = htonl(file_stat.st_size);
                
                if( (bytes_sent = send(sockc, file_size, 4, 0)) < 0 ){
                    printf("(%d)  Error sending data: %d\n", getpid(), *file_size);
                    close(sockc);
                    freeSupportData(err, ok, quit, filename, buff); 
                    return  0;
                }
                
                // sending time stamp
                ts = (int*) malloc(4*sizeof(char));
                *ts = htonl(file_stat.st_mtime);
                if( (bytes_sent = writen(sockc, (void*)ts, 4)) < 0 ){
                    printf("(%d)  Error sending data: %d\n", getpid(), *file_size);
                    close(sockc);
                    freeSupportData(err, ok, quit, filename, buff);
                    return  0;
                }
                
                // sending file
                off_t offset = 0;            
                if( (bytes_sent = sendfile (sockc, fd, &offset, file_stat.st_size)) < 0 ){
                    printf("(%d)  Error sending data: %d\n", getpid(), *file_size);
                    close(sockc);
                    freeSupportData(err, ok, quit, filename, buff);                    
                    return  0;
                }else {
                    printf("(%d)  File sent\n", getpid());
                    close(fd);
                    free(buff);
                    continue;
                }
                
            }
        } else if ( n == 0 ){
            printf("(%d)  no response after %d seconds\n", getpid(), t);
            close(sockc);
            freeSupportData(err, ok, quit, filename, buff);
            return 0;
        }
    }
    
    return 0;
}

int handle_client_xdr( int sockc ){
    
    int n;
    int fd;
    struct stat file_stat;
    
    FILE *stream_r = fdopen(sockc, "r");
    FILE *stream_w = fdopen(sockc, "w");
    XDR xdr_rec, xdr_send;
    char* filename;
    char buff[BUFSIZE];
    int size, timestamp;
    tagtype tag;
    message to_send, to_rec;
    to_rec.message_u.filename = (char*) malloc(sizeof(char)*256);
    
    struct timeval tval;
    fd_set cset;    
    int t = 15; tval.tv_usec = 0;  
    
    while(1){
        
        tval.tv_sec = t;
        FD_ZERO(&cset); FD_SET(sockc, &cset);        
        if( ( n = select(FD_SETSIZE, &cset, NULL, NULL, &tval) ) == -1 ){
            perror("Error ");
            xdr_destroy(&xdr_rec);
            close(sockc);
            return -1;
        }
        if (n>0){
            // receiving message
            xdrstdio_create(&xdr_rec, stream_r, XDR_DECODE);   
            if( !xdr_message(&xdr_rec, &to_rec) ){
                fprintf (stderr, "Could not receive tagtype\n");                
                xdr_destroy(&xdr_rec);
                return 1;
            } //printf("TAG ricevuto: %d\n", to_rec.tag);
            xdr_destroy(&xdr_rec);
            
            // if it is QUIT: close connection with client
            if( to_rec.tag == QUIT ){
                printf("(%d)  client wants to close connection\n", getpid());
                close(sockc);
                xdr_destroy(&xdr_rec);
                printf("(%d)  connection closed\n", getpid());
                return 1;
            }
            filename = strdup(to_rec.message_u.filename);
            free(to_rec.message_u.filename);
            
            // open the file            
            fd = open(filename, O_RDONLY);
            if(fd < 0){
                // can't open file
                xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
                to_send.tag = ERR;
                if( !xdr_message(&xdr_send, &to_send) ){
                    printf("(%d)  Error sending data ERR\n", getpid()); 
                    return -1;                   
                }
                xdr_destroy(&xdr_send);
                close(sockc);                
                return 0;
                
            } 
            printf("(%d)  File '%s' opened\n", getpid(), filename);
            free(filename);
            
            // get stas from file
            if( fstat(fd, &file_stat) < 0 ){
                
                xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
                to_send.tag = ERR;
                if( !xdr_message(&xdr_send, &to_send) ){
                    printf("(%d)  Error sending data ERR\n", getpid()); 
                    return -1;                   
                }
                xdr_destroy(&xdr_send);
                close(sockc);                
                return 0;
                
            } else {
                size = file_stat.st_size;
                printf("(%d)  File size: %d\n", getpid(), size);
                
                /* file opened correctly *
                 * send OK               */
                xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
                tag = OK;
                if( !xdr_tagtype(&xdr_send,&tag) ){
                    printf("(%d) Error sending data OK\n", getpid());
                    return -1;   
                }
                xdr_destroy(&xdr_send);
                
                /* sending file length */
                xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
                if(!xdr_int(&xdr_send, &size)){
                    printf("(%d) Error sending file length\n", getpid());
                    return -1;   
                }
                xdr_destroy(&xdr_send);
                
                /* sending file */
                int nleft = size;
                int nread = 0, nwrite = 0;
                
                memset(buff,0,sizeof(buff));
                printf("val: %d\n", (int)buff[0]);
                while ( ( (nread = read(fd, buff, BUFSIZE)) > 0) && (nleft > 0) ){
                    
                    if(nread<=0){
                        printf("Error during the reading of file\n");
                        return -1;
                    }
                    
                    nwrite = writen(sockc, buff, nread);
                    if(nwrite<0){
                        printf("Error during send()\n");
                        return -1;
                    }
                    nleft -= nread;
                    printf("Sent %d bytes\tread %d bytes, and we hope :- %d bytes\n", nwrite, nread, nleft);
                    //memset( buff, 0, BUFLEN );
                 
                }
                if( (size%4) && (writen(sockc, buff , size%4) < 0) )  {
                    printf("(%d) Error sending data\n", getpid());
                    return -1;   
                }
                close(fd);
                
                /* sending timestamp */
                timestamp = file_stat.st_mtime;
                xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
                if(!xdr_int(&xdr_send, &timestamp)){
                    printf("(%d) Error sending timestamp\n", getpid());
                    return -1;   
                }
                xdr_destroy(&xdr_send);
                printf("(%d) File timestamp: %d\n", getpid(), timestamp);     
                 
                continue;
                
            }
                
            
        } else if ( n == 0 ){
            printf("(%d)  no response after %d seconds\n", getpid(), t);
            close(sockc);
            return 0;
        }
    }
    
    return 0;
      
    
    
}

int readn(int s, char *ptr, size_t len){
    
    ssize_t nread; size_t nleft;
    
    for(nleft = len; nleft>0 ; ){
        nread = recv(s, ptr, nleft, 0);
        if(nread > 0){
            nleft -= nread;
            ptr += nread;
        }
        else if (nread == 0)
            break;
        else return nread;
    }
    return (len-nleft);
    
}

int writen(int s, char *ptr, size_t nbytes){
        
    size_t nleft; ssize_t nwritten;
    
    for( nleft = nbytes; nleft > 0; ){
        
        nwritten = send(s, ptr, nleft, 0);
        if( nwritten < 0)
            return nwritten;
        else {
            nleft -= nwritten;
            ptr += nwritten;
        }        
    }
    return (nbytes-nleft);
}

void freeSupportData(char* buf1, char* buf2, char* buf3, char* buf4, char* buf5){
    free(buf1);
    free(buf2);
    free(buf3);
    free(buf4);
    free(buf5);
}
