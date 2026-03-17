#include "csapp.h"

#define MAX_NAME_LEN 256
#define NPROC 5

void echo(int connfd);

pid_t pids[NPROC];

void handler_SIGINT(int sig)
{
    int i;
    for (i = 0; i < NPROC; i++) {
        Kill(pids[i], SIGINT);
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, i;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    char client_ip_string[INET_ADDRSTRLEN];
    char client_hostname[MAX_NAME_LEN];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);

    Signal(SIGINT, handler_SIGINT);

    for (i = 0; i < NPROC; i++) {
        if ((pids[i] = Fork()) == 0) {
            // child process infinite loop
            while (1) {
                clientlen = (socklen_t)sizeof(clientaddr);
                connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

                Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAX_NAME_LEN, 0, 0, 0);
                Inet_ntop(AF_INET, &clientaddr.sin_addr, client_ip_string, INET_ADDRSTRLEN);
                
                printf("server connected to %s (%s) by process %d on port %d\n", client_hostname, client_ip_string, getpid(), port);

                echo(connfd);
                Close(connfd);
            }
        }
    }

    // parent process waits for signals
    while (1) {
        
    }
    exit(0);
}