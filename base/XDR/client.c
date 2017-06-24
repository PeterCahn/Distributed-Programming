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

#define MAXLEN 32
#define BUFSIZE 256

#define GET_B   "GET"
#define QUIT_B  "QUIT\r\n"
#define OK_B    "OK\r\n" 
#define ERR_B   "ERR\r\n"

#define MAX_REC_BYTE 5


int readn(int s, char *ptr, size_t len);
int writen(int s, char *ptr, size_t nbytes);
int createTcpConnection(const char* serverAddress, int port);
void closeTcpConnection(int s);

int clientTask(int s);
int getCase(const int s, const char* command);
int quitCase(const int s);

int clientTask_xdr(int s);
int getCase_xdr(int s, const char* command);
int quitCase_xdr(const int s);

int main(int argc, char * argv[]){
    
    int s, xdr_mode = 0, i, port, ret;
    char address[64];
    
    for(i=0; i<argc; i++){
        if( !strcmp(argv[i], "-x") )
            xdr_mode = 1;
    }
    
    if(xdr_mode){
        port = atoi(argv[3]);
        strcpy(address, argv[2]);
    }else if(argc == 3){
        port = atoi(argv[2]);
        strcpy(address, argv[1]);        
    }else{
        printf("Format: <program_name> [<-x>] <server_address> <port> \n");
        exit(-1);
    }
    
    
    s = createTcpConnection(address, port);
    
    while(1){
        if(xdr_mode)
            ret = clientTask_xdr(s);
        else
            ret = clientTask(s);
        
        if( ret <= 0 )
            break;
        
    }
    
    closeTcpConnection(s);
}

int createTcpConnection(const char* serverAddress, int port){
    
    int s;
    struct sockaddr_in server;    
    
    // creating sockeft
    s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);       
    
    // init struct sockaddr_in
    server.sin_family       = AF_INET;
    server.sin_port         = htons(port);
    inet_aton( serverAddress, &(server.sin_addr) );    
    if( connect(s, (struct sockaddr*) &server, sizeof(server) ) == -1 ){
        printf("connect() failed\n");
        exit(-1);
    }
    
    return s;
}

void closeTcpConnection(int s){
    close(s);    
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

int clientTask(int s){
    
    int n, ret;
    
    char *str;
    size_t l = MAXLEN;
    
    printf("Enter the command:\n");
        
    // read the line        
    str = (char*) malloc(MAXLEN*sizeof(char));            
    if( ( n = getline(&str, &l, stdin) ) == 0 ){
        printf("can't read the command\n");
        free(str);
        return -1;
    }
    
    assert(str != NULL);
    
    if( !strncmp(str, GET_B, 3) ){          // GET_B: "GET"
        // command is GET
        ret = getCase(s, str);
        
    } else if ( !strncmp(str, QUIT_B, 4) ){     // QUIT_B: "QUIT"
        // command is QUIT
        ret = quitCase(s);
    }else {
        // command wrong
        printf("List of available commands:\n1) GET <filename>\n2) QUIT\n");
        return 1;
    }
    
    free(str);
    
    return ret;
    
}

int clientTask_xdr(int socket){    
    
    int n, ret;
    
    char *str;
    size_t l = MAXLEN;
    
    printf("Enter the command:\n");
        
    // read the line        
    str = (char*) malloc(MAXLEN*sizeof(char));
    if( ( n = getline(&str, &l, stdin) ) == 0 ){
        printf("can't read the command\n");
        free(str);
        return -1;
    }
    
    assert(str != NULL);
    
    if( !strncmp(str, GET_B, 3) ){         // GET_B: "GET"
        // command is GET
        ret = getCase_xdr(socket, str);
        
    } else if ( !strncmp(str, QUIT_B, 4) ){     // QUIT_B: "QUIT"
        // command is QUIT
        ret = quitCase_xdr(socket);
    }else {
        // command wrong
        printf("List of available commands:\n1) GET <filename>\n2) QUIT\n");
        return 1;
    }
    
    free(str);
    
    return ret;
}

int getCase_xdr(int s, const char* command){
    
    FILE *stream_w = fdopen(s, "w");
    FILE *stream_r = fdopen(s, "r");
    XDR xdr_send, xdr_rec; 
    message to_rec;
    message *to_send = (message*) malloc(sizeof(message));
    int size = 0, timestamp = 0;    
    char nomefile[256];
    
    
    // sending message
    sscanf(command, "%*s %s", nomefile); // extract nomefile from command    
    to_send->tag = GET;
    to_send->message_u.filename = strdup(nomefile);
    
    xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
    if( !xdr_message(&xdr_send, to_send) ){
        printf("\t--- could not encode message\n");
        xdr_destroy(&xdr_send);
        return -1;
    }
    xdr_destroy(&xdr_send);
    fflush(stream_w);
    
    /* receiving message */
    xdrstdio_create(&xdr_rec, stream_r, XDR_DECODE);    
    if( !xdr_message(&xdr_rec, &to_rec) ){
        printf("\t--- error receiving response from server\n");
        xdr_destroy(&xdr_rec);
        return -1;   
    }
    xdr_destroy(&xdr_send);
    fflush(stream_r);
    if( to_rec.tag == ERR){
        printf("\t--- ERR message received\n");
        return -1;
    } else if (to_rec.tag == OK)
        printf("\t--- OK message received from server\n");
    
    /* storing file size */   
    size = to_rec.message_u.fdata.contents.contents_len;
    printf("\t--- received file size: %d\n", size);      
    
    /* prepare file to be written */
    FILE* received_file = fopen(nomefile, "w");
    if( received_file < 0){
        printf("\t--- error opening file on writing\n");        
        return -1;
    }    
    
    /* writing file */
    char *buff = strdup(to_rec.message_u.fdata.contents.contents_val);    
    
    if(fwrite(buff, size, 1, received_file) < 0){
        printf("\t--- error on writing file '%s' on file system\n",nomefile);
    }
    
    fclose(received_file);
    //printf("\t--- File correctly written on local storage\n");
    
    /* storing file timestamp */
    timestamp = to_rec.message_u.fdata.last_mod_time;
    printf("\t--- received file timestamp: %d\n", timestamp);     
    
    return 1;
}

int getCase(const int s, const char* command){
    
    int len, bytes_rec = -1, bytes_sent, file_size;
    char *filename, *cmd, *buf, *buff_file;    
    int *file_size_buff, *ts_buff;
    FILE *received_file;
    
    buf = (char*) malloc(MAX_REC_BYTE*sizeof(char));
    filename = (char*) malloc(MAXLEN*sizeof(char));
    
    sscanf(command, "%*s %s", filename);
    len =  4 + strlen(filename) + 2; // GET filename\r\n
    cmd = (char*) malloc((len+1)*sizeof(char));
    sprintf(cmd, "%s %s\r\n", GET_B, filename);
    
    // sending GET command
    if( ( bytes_sent = send(s, cmd, strlen(cmd), 0) ) < 0){
        printf("send() failed");
        free(buf);
        free(filename);
        free(cmd);
        return bytes_rec;
    }
    
    // verify if first byte is '-'    
    if( ( bytes_rec = readn(s, buf, 1) ) < 0 ){
        printf("A problem occurred during starting receiving file. Retry.\n");
        free(buf);
        free(cmd);
        free(filename);
        return bytes_rec;
    } else if( bytes_rec == 0){
        printf("Connection closed on server-side: connection will be closed\n");
        free(buf);
        free(cmd);
        free(filename);
        return bytes_rec;
        
    } else if(buf[0] == '-'){
        // receive other 5 bytes: 'ERR\r\n' -> print & stop
        bytes_rec = readn(s, buf, 5);
        printf("%s", buf);
        free(buf);
        free(cmd);
        free(filename);
        return -1;
    }
    
    // receiving "OK\r\n"
    if( ( bytes_rec = readn(s,buf,4) )  <= 0 ){
        printf("A problem occurred during starting receiving 'OK' from server.\n");
        free(buf);
        free(cmd);
        free(filename);
        return bytes_rec;
    } else printf("%s", buf);
        
    // receiving file_size    
    file_size_buff = (int*) malloc(4*sizeof(char));
    if( ( bytes_rec = recv(s, file_size_buff, 4, 0) ) <= 0 ){
        printf("A problem occurred during receiving file size.\n");
        free(buf);
        free(cmd);
        free(filename);
        return bytes_rec;
    } else {
        file_size = ntohl(*file_size_buff);
        printf("\t--- File size to receive: %d\n", file_size);
    }
    
    // receiving timestamp
    ts_buff = (int*) malloc(4*sizeof(char));
    if( ( bytes_rec = recv(s, ts_buff, 4, 0) ) <= 0 ){
        printf("A problem occurred during starting receiving filestamp.\n");
        free(buf);
        free(cmd);
        free(filename);
        return bytes_rec;
    } else printf("\t--- File timestamp: %d\n", ntohl(*ts_buff));
    free(ts_buff);

    // start receiving file            
    buff_file = (char*) malloc(file_size*sizeof(char));    
    if( ( bytes_rec = recv(s, buff_file, file_size, 0) ) <= 0 ){
        printf("A problem occurred during receiving file. Retry.\n");
        free(buf);
        free(cmd);
        free(filename);
        return bytes_rec;
    } printf("\t--- Received file size: %d\n", bytes_rec);
    
    // writing received data on a nuew file
    received_file = fopen(filename, "w");
    if( received_file == NULL){
        printf("Error opening file on writing\n");        
        free(buf);
        free(cmd);
        free(filename);
        return bytes_rec;        
    }
    fwrite( buff_file, bytes_rec, 1, received_file);
    fclose(received_file);
    
    return 1;
}


int quitCase_xdr(const int s){
    XDR xdr_send;
    FILE* stream_w = fdopen(s, "w");
    message to_send;
    to_send.tag = QUIT;    
    
    xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
    if( !xdr_message(&xdr_send, &to_send) ){
        printf("Error sending QUIT tag\n");
        return -1;
    }
    xdr_destroy(&xdr_send);
    fflush(stream_w);
    return -1;
    
}

int quitCase(const int s){
    
    int bytes_rec;
    char *cmd = (char*) malloc(6*sizeof(char)); // Q U I T \r \n
        strcpy(cmd, QUIT_B);
        if( ( bytes_rec = writen(s, cmd, strlen(cmd) ) ) < 0 ){
            printf("send() failed\n");
            free(cmd);
            return bytes_rec;
        } 
        
    return -1;
}


