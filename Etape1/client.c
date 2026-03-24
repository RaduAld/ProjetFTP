/*
 * echoclient.c - An echo client
 */
#include "csapp.h"
#include "types.h"

void handle_response(response_t *resp, char *filename) {
    switch(resp->type){
        case GET:
            if (resp->status == 0) {
                //printf("Response from server: %.*s\n", (int)resp->dataSize, resp->data);
                char path[MAXCHAR] = "./repClient/";
                strcat(path, filename);
                int fd = Open(path, O_WRONLY | O_CREAT, 0644);
                Rio_writen(fd, resp->data, resp->dataSize);
                Close(fd);
            } else {
                fprintf(stderr, "%s\n", resp->data);
            }
            break;
        case PUT:
            printf("Received PUT response from server\n");
            break;
        case LS:
            printf("Received LS response from server\n");
            break;
        default:
            fprintf(stderr, "Unknown response type\n");
            return;
    }
}

int main(int argc, char **argv)
{
    int clientfd, port;
    char *host, buf[MAXLINE];

    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    host = argv[1];
    port = atoi(argv[2]);

    /*
     * Note that the 'host' can be a name or an IP address.
     * If necessary, Open_clientfd will perform the name resolution
     * to obtain the IP address.
     */
    clientfd = Open_clientfd(host, port);
    
    /*
     * At this stage, the connection is established between the client
     * and the server OS ... but it is possible that the server application
     * has not yet called "Accept" for this connection
     */
    printf("client connected to server OS\n"); 

    while (Fgets(buf, MAXLINE, stdin) != NULL) {

        //Send request to server
        request_t req;
        if (strcmp(strtok(buf, " "), "get") == 0) {
            req.type = GET;
        } else if (strcmp(strtok(buf, " "), "put") == 0) {
            req.type = PUT;
        } else if (strcmp(strtok(buf, " "), "ls") == 0) {
            req.type = LS;
        } else {
            fprintf(stderr, "Invalid command. Use 'get', 'put' or 'ls'.\n");
            continue;
        }
        strncpy(req.filename, strtok(NULL, " \n"), MAXLINE);

        Rio_writen(clientfd, &req, sizeof(request_t));

        //Receive response from server
        response_t resp;
        
        if (Rio_readn(clientfd, &resp, sizeof(response_t)) > 0) {
            handle_response(&resp, req.filename);
            break;
        } else { /* the server has prematurely closed the connection */
            fprintf(stderr, "Serveur a fermé la connexion\n");
            break;
        }
    }
    Close(clientfd);
    exit(0);
}
