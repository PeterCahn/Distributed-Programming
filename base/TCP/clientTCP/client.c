#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#define MAXLEN 32
#define BUFLEN 256

#define MAX_REC_BYTE 5


int readn(int s, char *ptr, size_t len);
int writen(int s, char *ptr, size_t nbytes);
int createTcpConnection(const char* serverAddress, const char* port);
int clientInitTcpConnection(const char* address, const char* port);
void closeTcpConnection(int s);

int clientTask(int s);
int getCase(const int s, const char* file);
int quitCase(const int s);

int main(int argc, char * argv[]){
    
    int s;        
    
    s = clientInitTcpConnection(argv[1], argv[2]);
    
    while(1){
        
        if( clientTask(s) <= 0 )
            break;        
        
    }
    
    closeTcpConnection(s);
}

void closeTcpConnection(int s){
    close(s);    
}

int clientTask(int s){
    
    char str[BUFLEN];
    
    printf("Insert file names (no GET), one per line (end with EOF - press CTRL+D):\n");
    
    // read the line
    if( fgets(str, BUFLEN, stdin) == NULL ){
        return quitCase(s);        
    }
    return getCase(s, str);
    
}

int getCase(const int s, const char* filename){
    
    int bytes_rec = 0, bytes_sent, file_size;
    
    char buf[BUFLEN];
    char cmd[BUFLEN];
    char file[BUFLEN];
    
    sprintf(file, "%s", filename);
    file[strlen(file)-1] = '\0';
    
    
    sprintf(cmd, "GET %s\r\n", filename);
    
    // sending GET command
    if( ( bytes_sent = writen(s, (void *) cmd, strlen(cmd)) ) < 0){
        printf("send() failed");
        return bytes_rec;
    }
    
    // verify if first byte is '-'    
    if( ( bytes_rec = readn(s, buf, 1) ) < 0 ){
        printf("A problem occurred during starting receiving file. Retry.\n");
        return bytes_rec;
    } else if( bytes_rec == 0){
        printf("Connection closed server-side: connection will be closed\n");
        return bytes_rec;
        
    } else if(buf[0] == '-'){
        // receive other 5 bytes: 'ERR\r\n' -> print & stop
        bytes_rec = readn(s, buf, 5);
        printf("%s", buf);
        return -1;
    }
    
    // receiving "OK\r\n"
    if( ( bytes_rec = readn(s,buf,4) )  <= 0 ){
        printf("A problem occurred during starting receiving 'OK' from server.\n");
        return bytes_rec;
    } else printf("%s", buf);
    
    // receiving file_size
    int *file_size_buff = (int*) malloc(4*sizeof(char));
    if( ( bytes_rec = recv(s, file_size_buff, 4, 0) ) <= 0 ){
        printf("A problem occurred during receiving file size.\n");
        free(file_size_buff);
        return bytes_rec;
    } else {
        file_size = ntohl(*file_size_buff);
        printf("\t--- File size to receive: %d\n", file_size);
    }
    free(file_size_buff);
    
    // receiving timestamp
    int *ts = (int*) malloc(4*sizeof(char));
    if( ( bytes_rec = recv(s, ts, 4, 0) ) <= 0 ){
        printf("\t--- A problem occurred during starting receiving filestamp.\n");
        free(ts);
        return bytes_rec;
    } else printf("\t--- File timestamp: %d\n", ntohl(*ts));
    free(ts);
    
    // start receiving file
    FILE* received_file = fopen(file, "w");
    if( received_file == NULL){
        printf("\t--- error opening file '%s' on writing\n", file);        
        return -1;
    }
    
    /* read all incoming bytes */
    char* buff_file = (char*) malloc(file_size*sizeof(char));    
    if( ( bytes_rec = readn(s, buff_file, file_size) ) <= 0 ){
        printf("A problem occurred during receiving file. Retry.\n");
        free(buff_file);
        return bytes_rec;
    }
    
    /* writing on local file system */
    if(fwrite(buff_file, file_size, 1, received_file) < 0){
        printf("\t--- error on writing file '%s' on file system\n",filename);
    }else printf("\t--- File written on local file system\n");
    free(buff_file);    
    
    fclose(received_file);
    
    return 1;
}

int quitCase(const int s){
    
    int bytes_rec;
    char cmd[6] = "QUIT\r\n";    
    if( ( bytes_rec = writen(s, cmd, 6 ) ) < 0 ){
        printf("send() failed\n");        
        return bytes_rec;
    } 
    
    return -1;
}



int clientInitTcpConnection(const char* address, const char* port){
    
    struct addrinfo hints, *servinfo, *res;
    int rv;
    int s;
    char ipstr[INET6_ADDRSTRLEN];
    void *addr = NULL;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // AF_INET6 o AF_UNSPEC 
    hints.ai_socktype = SOCK_STREAM;
    if( ( rv = getaddrinfo( address, port, &hints, &servinfo )) != 0){
        printf("getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
        
    for( res = servinfo; res != NULL ; res = res->ai_next){
               
        if( ( s = socket(res->ai_family, res->ai_socktype, res->ai_protocol) ) < 0 ){
	  printf("Error creating master socket\n");
	  continue;
	}

	// let reuse of socket address on local machine
	int enable = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
	  printf("setsockopt(SO_REUSEADDR) failed");
	  close(s);
	  exit(-1);
	}
		
	if( connect(s, res->ai_addr, res->ai_addrlen) < 0 ){
	    perror("connect");
	    close(s);
	    continue;
	}
		
	// get the pointer to the address itself
        if (res->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
            addr = &(ipv4->sin_addr);
        } else { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)res->ai_addr;
            addr = &(ipv6->sin6_addr);
        }
        
        // convert the IP to a string and print it:
        inet_ntop(res->ai_family, addr, ipstr, sizeof ipstr);
        
        break;
        
    }
    freeaddrinfo(servinfo);
    
    if(res == NULL){
    	printf("(%d) Impossible to connect to the server\n", getpid());
    	exit(-1);
    }
    
    assert(res != NULL);
    
    printf("(%d) socket created\n", getpid());
    printf("(%d) connected to server %s:%s\n", getpid(), ipstr, port );
    
    return s;
    
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


