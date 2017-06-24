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
#include <rpc/xdr.h>
#include <ctype.h>

#include "../types.h"

#define MAXLEN 32
#define BUFLEN 256

#define MAX_REC_BYTE 5

int readn(int s, char *ptr, size_t len);
int writen(int s, char *ptr, size_t nbytes);
int clientInitTcpConnection(const char* address, const char* port);

int clientTask(int s, const char* type, const char* input);

int main(int argc, char * argv[]){
    
    int s;        
    
    if(argc != 5){
    	printf("usage: %s, <server address> <port> <ENCODE or DECODE> <file>\n", argv[0]);
    	exit(1);
    }
    
    s = clientInitTcpConnection(argv[1], argv[2]);
    
    clientTask(s, argv[3], argv[4]);
     
    close(s);
    return 0;
}


int clientTask(int s, const char* type, const char* input){
    
    FILE *stream_w = fdopen(s, "w");
    FILE *stream_r = fdopen(s, "r");
    XDR xdr_send, xdr_rec; 
    
    FILE* inputfile;
    int n = 0, i = 0;
    operation op;
    float number;
    Request req;
    Response res;

    if( (inputfile = fopen(input, "r")) == NULL){
    	printf("Error opening file '%s'\n", input);
    	fclose(stream_w);
	   	fclose(stream_r);
    	exit(1);
    }
    
    /* leggi numero righe */
    while(!feof(inputfile)){
	   	fscanf(inputfile, "%f\n", &number);
    	n++;
    }
    rewind(inputfile);    
    
    float numbers[n];	
    /* leggi numeri */
	while(!feof(inputfile)){
	   	fscanf(inputfile, "%f\n", &numbers[i]);
    	i++;
    }
    
    /* crea Request */
    if( strcmp(type, "ENCODE") == 0)
    	op = ENCODE;
    else if( strcmp(type, "DECODE") == 0)
    	op = DECODE;
    req.op = op;
    req.data.data_len = n;
    req.data.data_val = numbers;
    
    /* invio Request */
    xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE);
    if( !xdr_Request(&xdr_send, &req) ){
        printf("\t--- could not encode Request\n");
    	fclose(stream_w);
	   	fclose(stream_r);
        xdr_destroy(&xdr_send);
        return -1;
    }
    xdr_destroy(&xdr_send);
    fflush(stream_w);

	/* ricezione Response */
	memset(&res, 0, sizeof(res));
	xdrstdio_create(&xdr_rec, stream_r, XDR_DECODE);    
    if( !xdr_Response(&xdr_rec, &res) ){
        printf("\t--- error receiving response from server\n");
        xdr_destroy(&xdr_rec);
    	fclose(stream_w);
	   	fclose(stream_r);
        return -1;   
    }
    xdr_destroy(&xdr_rec);
    fflush(stream_r);
    
    if(res.success == FALSE){
    	printf("Error : messaggio precedentemente inviato aveva un formato non corretto\n");
    	exit(1);
    }
    
    FILE *output = fopen("output.txt", "w");
    for(i = 0; i<res.data.data_len; i++){
		fprintf(output, "%f\n", res.data.data_val[i]);
    }
    fclose(output);
    fclose(stream_w);
	fclose(stream_r);
	
    
    return 0;
    
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


