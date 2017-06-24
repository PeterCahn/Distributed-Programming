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
#include <assert.h>
#include <errno.h>

#define BUFLEN 32
#define DATAOUT 12
#define DATAIN 8

int clientInitUdpConnection(const char* address, const char* port, struct addrinfo *servaddr);

int main (int argc, char *argv[]) {

	int s, n;
	struct addrinfo server;
	uint32_t id, op1, op2, id_rec, res;
	char filename[BUFLEN] = "output.txt";
	FILE *output;
    struct timeval tval;
    fd_set sset;
    int t = 3, ntentativi = 0;
	
	if(argc != 6){
        printf("usage: %s <address> <port> <id> <op1> <op2>\n", argv[0]);
        exit(1);   
    }
    
    id = atoi(argv[3]);
    op1 = atoi(argv[4]);
    op2 = atoi(argv[5]);    
	
	s = clientInitUdpConnection(argv[1], argv[2], &server);
	
	/* send id */
	uint32_t out = htonl(id);
	if( sendto(s, &out, 4, 0, server.ai_addr, server.ai_addrlen) < 0){
		perror("sendto");
		return 1;
	}
	/* send op1 */
	out = htonl(op1);
	if( sendto(s, &out, 4, 0, server.ai_addr, server.ai_addrlen) < 0){
		perror("sendto");
		return 1;
	}
	/* send op2 */
	out = htonl(op2);
	if( sendto(s, &out, 4, 0, server.ai_addr, server.ai_addrlen) < 0){
		perror("sendto");
		return 1;
	}

    while(1){
		tval.tv_sec = t; tval.tv_usec = 0;
		FD_ZERO(&sset);     // azzero struttura dati per file descriptor
		FD_SET(s, &sset);   // inserisco sockc tra i fd		
		
		if( (n = select(FD_SETSIZE, &sset, NULL, NULL, &tval)) == -1 ){            
		    perror("select() failed ");
		    close(s);
		    return 0;
		}
		if(n>0){
			
			printf("ricevuto qualcosa\n");
			
			/* rec id */
			if( recvfrom(s, &id_rec, 4, 0, server.ai_addr, &(server.ai_addrlen)) < 0){	
				printf("Errore nella ricezione dei dati\n");
				exit(1);
			}
				
			/* rec res */
			if( recvfrom(s, &res, 4, 0, server.ai_addr, &(server.ai_addrlen)) < 0){	
				printf("Errore nella ricezione dei dati\n");
				exit(1);
			}
			
			id_rec = ntohl(id_rec);
			if(id != id_rec)
				continue;
			res = ntohl(res);
			printf("result: id=%d res=%d\n", id_rec, res);	
			
			break;
		    
		}else if(n == 0){
		    printf("(%d)  no response after %d seconds\n", getpid(), t);
		    
			if(ntentativi == 3)
				break;
				
			ntentativi++;
			printf("tentativo n. %d\n",ntentativi);
			/* send id */
			uint32_t out = htonl(id);
			if( sendto(s, &out, 4, 0, server.ai_addr, server.ai_addrlen) < 0){
				perror("sendto");
				return 1;
			}
			/* send op1 */
			out = htonl(op1);
			if( sendto(s, &out, 4, 0, server.ai_addr, server.ai_addrlen) < 0){
				perror("sendto");
				return 1;
			}
			/* send op2 */
			out = htonl(op2);
			if( sendto(s, &out, 4, 0, server.ai_addr, server.ai_addrlen) < 0){
				perror("sendto");
				return 1;
			}
			
		    output = fopen(filename, "w");  
		    char c = '2';      
			putc(c, output);
			fclose(output);
			
		}
    }
    
    if(id == id_rec){
		char str[BUFLEN];
		sprintf(str, "0 %d", res);
		output = fopen(filename, "w");
		fputs(str, output);
		fclose(output);
	}else{	
		output = fopen(filename, "w");  
        char c = '1';
		putc(c, output);
		fclose(output);
	}
    
	close(s);
	return 0;
}

int clientInitUdpConnection(const char* address, const char* port, struct addrinfo *server){

	struct addrinfo hints, *servinfo, *res;
    int rv, s;
    char ipstr[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // AF_INET6 o AF_UNSPEC 
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
    if( ( rv = getaddrinfo( address, port, &hints, &servinfo )) != 0){
        printf("getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for( res = servinfo; res != NULL; res = res->ai_next){
        void *addr;
        
		if( ( s = socket(res->ai_family, res->ai_socktype, res->ai_protocol) ) < 0 ){
		    perror("Error creating master socket");		  
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
        
        server->ai_addr = res->ai_addr;
        server->ai_addrlen = res->ai_addrlen;
        // convert the IP to a string and print it:
        inet_ntop(res->ai_family, addr, ipstr, sizeof ipstr);
        break;
    }
    
    if(res == NULL){
    	printf("not able to create socket\n");
    	exit(-1);
    }
    
	printf("(%d) socket created\n", getpid());
    printf("(%d) connected to %s:%s\n", getpid(), ipstr, port );
    freeaddrinfo(servinfo);
    
    return s;

}


