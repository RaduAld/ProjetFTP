/*
 * echoclient.c - An echo client
 */
#include "csapp.h"
#include "types.h"



void handle_response(response_t *resp, char *filename, bool isFirst) {
    switch(resp->type){
        case GET:
            int fd;
            if (resp->status == 0) {
                //printf("Response from server: %.*s\n", (int)resp->dataSize, resp->data);
                char path[MAXNAME] = "./repClient/";
                strcat(path, filename);
                if (isFirst) {
                    fd = Open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                } else {
                    fd = Open(path, O_WRONLY | O_APPEND, 0644);
                }
                // Ecriture des données dans le fichier
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

    //Send request to server
    request_t req;

    while (Fgets(buf, MAXLINE, stdin) != NULL) {

        if (strcmp(strtok(buf, " "), "get") == 0) {
            req.type = GET;
        } else if (strcmp(strtok(buf, " "), "put") == 0) {
            req.type = PUT;
        } else if (strcmp(strtok(buf, " "), "ls") == 0) {
            req.type = LS;
        } else if (strcmp(buf, "bye\n") == 0) {
            printf("Sending BYE request to server and closing connection.\n");
            req.type = BYE;
            return 0;
        } else {
            fprintf(stderr, "Invalid command. Use 'get', 'put', 'ls' or 'bye'.\n");
            continue;
        }
        strncpy(req.filename, strtok(NULL, " \n"), MAXNAME);

        hton_req(&req);
        Rio_writen(clientfd, &req, sizeof(request_t));

        //Receive response from server
        response_t resp;

        if (Rio_readn(clientfd, &resp, sizeof(response_t)) > 0) {
            ntoh_resp(&resp);
            handle_response(&resp, req.filename, true);
            while (!resp.endOfFile) {
                if (Rio_readn(clientfd, &resp, sizeof(response_t)) <= 0) {
                    fprintf(stdout, "Serveur a fermé la connexion\n");
                    break;
                }
                Sleep(2);
                ntoh_resp(&resp);
                handle_response(&resp, req.filename, false); // on ecrit dans le même fichier, en concaténant les données
            }
            fprintf(stdout, "End of file received\n");
            //break;
        } else { /* the server has prematurely closed the connection */
            fprintf(stderr, "Serveur a fermé la connexion\n");
            break;
        }

    }
    Close(clientfd);
    exit(0);
}
