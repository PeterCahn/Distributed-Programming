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

#define BUFLEN 256
#define DATAIN 3*4
#define DATAOUT 2*4
#define MAXCLIENTS 5

void handleClient(int socket);
int initUdpConnection(const char* port);
int enableInterruptChld();
int disableInterruptChld();

static int k = 0;
static int count = 0;
void signalHandler(int signal){
    
    int pid, stat;
    
    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
        printf("(%d) child %d terminated\n", getpid(), pid);
        count--;
    }
}


int main(int argc, char * argv[]){
      
    int s, i, pid[MAXCLIENTS];
    
    
    if(argc != 2){
        printf("usage: %s <port>\n", argv[0]);
        exit(1);   
    }
    k = 15;
    
     // Establish handling of SIGCHLD signal 
    if (signal(SIGCHLD, signalHandler) == SIG_ERR) {
        perror("Unable to set up signal handler for SIGCHLD");
        exit(1);
    }
    
    s = initUdpConnection(argv[1]);
    
    for(i = 0; i < MAXCLIENTS; i++){
    	if( (pid[i] = fork()) == 0 ){
    		handleClient(s);
    		exit(1);
    	}
    }
    wait(0);
    
    return 0;
}

void handleClient(int s){

	socklen_t csize;
	struct sockaddr_storage client;
    uint32_t op1, op2, id, res;
	
	while(1){
	
		/* receive id */
		csize = sizeof(client);
		if( recvfrom(s, &id, 4, 0, (struct sockaddr *) &client, &csize) < 0){
			perror("recvfrom");
			continue;
		}
		/* receive op1 */
		if( recvfrom(s, &op1, 4, 0, (struct sockaddr *) &client, &csize) < 0){
			perror("recvfrom");
			continue;
		}
		/* receive op2 */
		if( recvfrom(s, &op2, 4, 0, (struct sockaddr *) &client, &csize) < 0){
			perror("recvfrom");
			continue;
		}
		
		id = ntohl(id);
		op1 = ntohl(op1);		
		op2 = ntohl(op2);
		
		printf("(%d) id=%d op1=%d op2=%d\n", getpid(), id, op1, op2);
		
		res = (op1+op2)%k;
		printf("(%d) res=%d\n", getpid(), res);
		
		/* send id */
		id = htonl(id);
		if( sendto(s, &id, 4, 0, (struct sockaddr *) &client, csize) < 0){
			perror("sendto");
			continue;
		}
		/* send res */
		res = htonl(res);
		if( sendto(s, &res, 4, 0, (struct sockaddr *) &client, csize) < 0){
			perror("sendto");
			continue;
		}
		
	}
    
}


int initUdpConnection(const char* port){
    
    struct addrinfo hints, *servinfo, *res;
    int rv, s;
    char ipstr[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // AF_INET6 o AF_UNSPEC 
    hints.ai_socktype = SOCK_DGRAM;
    if( ( rv = getaddrinfo( "localhost", port, &hints, &servinfo )) != 0){
        printf("getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for( res = servinfo; res != NULL; res = res->ai_next){
        void *addr;
        
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
    printf("(%d) listening for UDP packets on %s:%s\n", getpid(), ipstr, port );
    
    return s;
    
    
}


