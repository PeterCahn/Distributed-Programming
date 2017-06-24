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

#include "../types.h"

#define BUFLEN 256
#define CMDLEN 8
#define MAXCLIENTS 3

int handle_request(int sockc);
void handle_client(int sockc);
int initTcpConnection(const char* port);
int readn(int s, char *ptr, size_t len);
int writen(int s, char *ptr, size_t nbytes);
int disableInterruptChld();
int enableInterruptChld();

static int count = 0;
static float k = 0;
void signalHandler(int signal){
    
    int pid, stat;
    
    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
        printf("(%d) child %d terminated\n", getpid(), pid);
        count--;
    }
}

int main(int argc, char * argv[]){
    
    struct sockaddr_in client[MAXCLIENTS];
    int s, sockc;
    socklen_t csize;
    int pid;   
    
    if(argc != 3){
        printf("usage: <program> <port> <k>\n");
        return 0;   
    }
    k = atof(argv[2]);
    
    // Establish handling of SIGCHLD signal 
    if (signal(SIGCHLD, signalHandler) == SIG_ERR) {
        perror("Unable to set up signal handler for SIGCHLD");
        exit(1);
    }
    
    s = initTcpConnection(argv[1]);
    
    csize = sizeof(client[0]);
    while(1){
        /* block signal SIGCHLD to prevent race conditions */
        if(disableInterruptChld() < 0){
            close(s);
            exit(-1);
        }
        
        /* accept new client
         * if there are less than MAXCLIENTS connected */        
        if(count < MAXCLIENTS){            
            printf("(%d) accepting new client\n", getpid());
            if( (sockc = accept(s, (struct sockaddr *) &client[count], &csize)) < 0){
                printf("Error accepting the client.\n"); 
                if(enableInterruptChld() < 0){
                    close(s);
                    exit(-1);
                }
                continue;
            } else
                count++;
            
        }else {            
            printf("(%d) Max number of connected clients reached!\n", getpid());                        
            close(sockc);            
            wait(0);
            count--;
            if(enableInterruptChld() < 0){
                close(s);
                exit(-1);
            }
            continue;            
        }
        if(enableInterruptChld() < 0){
            close(s);
            exit(-1);
        }
        
        if( (pid = fork()) < 0 ){
            printf("fork() failed");
            break;
        }else if(pid > 0){
            /* nel padre */
            printf("(%d) connection %d from client %s:%d\n", getpid(), count, inet_ntoa(client[count].sin_addr), ntohs(client[count].sin_port));
            close(sockc);
            continue;
        }else{
            /* nel figlio */
            close(s);
            handle_client(sockc);
            exit(0);
        }        
    }
    
    return 0;
}

int disableInterruptChld(){
    
    sigset_t mask;
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1){
        perror("sigprocmask: ");
        return -1;
    }
    return 1;
}

int enableInterruptChld(){
    
    sigset_t mask;
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1){
        perror("sigprocmask: ");
        return -1;
    }
    return 1;
}

int initTcpConnection(const char* port){
    
    struct addrinfo hints, *servinfo, *res;
    int rv;
    int s, bklog = 10;    
    char ipstr[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // AF_INET6 o AF_UNSPEC 
    hints.ai_socktype = SOCK_STREAM;
    if( ( rv = getaddrinfo( "localhost", port, &hints, &servinfo )) != 0){
        printf("getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for( res = servinfo; res != NULL; res = res->ai_next){
        void *addr;
        
        printf("creazione socket\n");
        
		if( ( s = socket(res->ai_family, res->ai_socktype, res->ai_protocol) ) < 0 ){
		    perror("Error creating master socket");		  
			continue;
		}
		
		// let reuse of socket address on local machine
		int enable = 1;
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
		    printf("setsockopt(SO_REUSEADDR) failed");
		    close(s);
		    continue;
		}    
		
		// binding socket    
		if( bind(s, res->ai_addr, res->ai_addrlen ) < 0 ){
		    perror("bind ");
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
    	printf("not able to create master socket\n");
    	exit(-1);
    }    
    
	printf("(%d) socket created\n", getpid());
        
    // start listening for bklog max client in the queue
    if( listen(s, bklog) < 0 ){
        printf("Error during listen()\n");
        close(s);
        exit(-1);
    }else printf("(%d) listening on %s:%s\n", getpid(), ipstr, port );
    
    return s;
    
    
}

void handle_client(int sockc){
    
    int n;
    struct timeval tval;
    fd_set cset;
    
    int t = 120; tval.tv_usec = 0;
    
    
    tval.tv_sec = t;
    FD_ZERO(&cset);         // azzero struttura dati per file descriptor
    FD_SET(sockc, &cset);   // inserisco sockc tra i fd
    
    if( (n = select(FD_SETSIZE, &cset, NULL, NULL, &tval)) == -1 ){            
        perror("select() failed ");
        close(sockc);
        return;
    }
    if(n>0){
        
        handle_request(sockc);
        
    }else if(n == 0){
        printf("(%d)  no response after %d seconds\n", getpid(), t);
        close(sockc);
        return;
    }
            
    printf("(%d) connection will be closed\n", getpid());
    close(sockc);
    return;
    
}

int handle_request(int sockc){
    
    FILE *stream_w = fdopen(sockc, "w");
    FILE *stream_r = fdopen(sockc, "r");
    XDR xdr_send, xdr_rec; 
    int i;
    Request req;
    Response res;
    
    /* ricezione request */
    xdrstdio_create(&xdr_rec, stream_r, XDR_DECODE);    
    if( !xdr_Request(&xdr_rec, &req) ){
        printf("\t--- error receiving request from client\n");
        
        /* invio res per notificare formato non corretto */   
        res.success = FALSE;
        xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE); 
		if( !xdr_Response(&xdr_send, &res) ){
		    printf("\t--- error sending response to client\n");
		    xdr_destroy(&xdr_send);
        	xdr_destroy(&xdr_rec);
		    return -1;
		}
	    xdr_destroy(&xdr_send);
        xdr_destroy(&xdr_rec);
        return -1;
    }
    xdr_destroy(&xdr_rec);
    
    /* request ricevuta correttamente
     * elaborazione response */
    res.success = TRUE;
    res.data.data_len = req.data.data_len;
    res.data.data_val = (float*) malloc(req.data.data_len*sizeof(float));
    
    for( i = 0; i < res.data.data_len; i++){
		if( req.op == ENCODE)
			res.data.data_val[i] = req.data.data_val[i] + k;
		else if( req.op == DECODE)
			res.data.data_val[i] = req.data.data_val[i] - k;    
    }
    
    /* sending response */
    xdrstdio_create(&xdr_send, stream_w, XDR_ENCODE); 
	if( !xdr_Response(&xdr_send, &res) ){
		printf("\t--- error sending response to client\n");
		xdr_destroy(&xdr_send);
		free(res.data.data_val);
		return -1;
	}
	xdr_destroy(&xdr_send);
	free(res.data.data_val);
    
    printf("(%d) client has been served: response was successfully sent|\n", getpid());
    
    return 1;
    
}

