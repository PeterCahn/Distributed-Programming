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
    int port, pid, stat;   
    
    if(argc != 2){
        printf("usage: <program> <port>\n");
        return 0;   
    }
    
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
    int s, bklog = 2*MAXCLIENTS;
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
    
    while(1){
        tval.tv_sec = t;
        FD_ZERO(&cset);         // azzero struttura dati per file descriptor
        FD_SET(sockc, &cset);   // inserisco sockc tra i fd
        
        if( (n = select(FD_SETSIZE, &cset, NULL, NULL, &tval)) == -1 ){            
            perror("select() failed ");
            close(sockc);
            return;
        }
        if(n>0){
            
            if( handle_request(sockc) < 0)
                break;
            
        }else if(n == 0){
            printf("(%d)  no response after %d seconds\n", getpid(), t);
            close(sockc);
            return;
        }
        
    }
    /* qualcosa è andato storto: la connessione sarà chiusa */
    printf("(%d) Connection will be closed\n", getpid());
    close(sockc);
    return;
    
}

int handle_request(int sockc){
    
    int bytes_rec, bytes_sent;
    int fd;
    char buff[BUFLEN], filename[BUFLEN];
    struct stat file_stat;
    
    char quit[CMDLEN] = "QUIT\r\n";
    char err[CMDLEN] = "-ERR\r\n";
    char ok[CMDLEN] = "+OK\r\n";
    
    
    if( (bytes_rec = recv(sockc, buff, BUFLEN, 0)) <= 0 ){        
        printf("(%d) Error during receiving command from client\n", getpid());
        return -1;
    } else if(bytes_rec == 0){
        printf("(%d) client closed current connection\n", getpid()); 
        return -1;
    } else {
        
        if(!strncmp(buff, quit, 6)){        
            /* il client ha inviato "QUIT" */
            printf("(%d) client wants to close connection\n", getpid());
            return -1;            
        }        
    }
    
    /* tutto ok:
     * il client ha richiesto un file: GET <filename> */
    sscanf(buff, "GET%s", filename);    
    fd = open(filename,O_RDONLY);
    if(fd < 0){
        /* impossibile aprire il file */        
        printf("(%d)  Error opening file %s\n", getpid(), filename);
        if( (bytes_sent = writen(sockc, err, 6)) <= 0 ){
            printf("(%d)  Error sending data ERR\n", getpid());
        }
        return -1;
    } 
    
    /* file aperto correttamente 
     * invio al client "OK" */
    printf("(%d)  File '%s' opened\n", getpid(), filename);
    if( (bytes_sent = writen(sockc, ok, 5)) < 0){
        printf("Error sending data OK\n");
        return  -1;        
    }
    
    /* ottieni stat dal file */
    if( fstat(fd, &file_stat) < 0 ){
        /* impossibile ottenere le info sul file */
        printf("(%d)  Error getting file stats.\n", getpid());
        if( (bytes_sent = writen(sockc, err, 6)) <= 0 ){
            printf("(%d)  Error sending data ERR\n", getpid());
        }
        return -1;
    }
    
    /* invio dimensione file */
    printf("(%d)  File size: %ld\n", getpid(), file_stat.st_size);
    int *file_size = (int*) malloc(4*sizeof(char));
    *file_size= htonl(file_stat.st_size);
    if( (bytes_sent = send(sockc, (void *)file_size, 4, 0)) < 0 ){
        printf("(%d)  Error sending data file_size: %d\n", getpid(), *file_size);
        return -1;
    }
    
    /* invio timestamp */
    int *ts = (int*) malloc(4*sizeof(char));
    *ts = htonl(file_stat.st_mtime);
    if( (bytes_sent = writen(sockc, (void*)ts, 4)) < 0 ){
        printf("(%d)  Error sending data timestamp: %d\n", getpid(), *ts);
        return -1;
    }
    
    /* invio file */
    int nleft = file_stat.st_size;
    int nread = 0, nwrite = 0;
    while ( nleft > 0  ){
    
    	nread = read(fd, buff, 1);
    	if(nread <= 0)
	    	break;
    		
        printf("read %d bytes from file\n", nread);
        if( nread <= 0 ){
            printf("Error during the reading of file\n");
            return -1;
        }
        
        nwrite = writen(sockc, buff, nread);
        printf("sent %d bytes on socket\n", nwrite);
        if( nwrite < 0){
            printf("Error during send()\n");
            return -1;
        }
        
        nleft -= nread;
        printf("left %d bytes\n", nleft);        
    }
    close(fd);
    
    printf("(%d)  File sent\n", getpid());
    return 1;
    
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
z