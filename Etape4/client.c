/*
 * echoclient.c - An echo client
 */
#include "csapp.h"
#include "types.h"



void handle_response(response_t *resp, request_t *req, bool isFirst) {
    switch(resp->type){
        case GET:
            int fd;
            if (resp->status == 0) {
                //printf("Response from server: %.*s\n", (int)resp->dataSize, resp->data);
                char path[MAXNAME] = "./repClient/";
                strcat(path, req->filename);
                if (isFirst) {
                    fd = Open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                } else {
                    fd = Open(path, O_WRONLY | O_APPEND, 0644);
                }
                // Ecriture des données dans le fichier
                lseek(fd, 0, SEEK_END);
                Rio_writen(fd, resp->data, resp->dataSize);
                req->offset += resp->dataSize; // on met à jour l'offset pour le prochain bloc de données
                Close(fd);
            } else {
                fprintf(stderr, "%s\n", resp->data);
            }
            break;
        case PUT:
            if (resp->status == 0) {
                printf("%.*s\n", (int)resp->dataSize, resp->data);
            } else {
                fprintf(stderr, "%.*s\n", (int)resp->dataSize, resp->data);
            }
            break;
        case LS:
            if (resp->status == 0 && resp->dataSize > 0) {
                printf("%.*s\n", (int)resp->dataSize, resp->data);
            } else {
                fprintf(stderr, "%.*s\n", (int)resp->dataSize, resp->data);
            }
            break;
        case RM:
            if (resp->status == 0) {
                printf("%.*s\n", (int)resp->dataSize, resp->data);
            } else {
                fprintf(stderr, "%.*s\n", (int)resp->dataSize, resp->data);
            }
            break;
        case LOGIN:
            if (resp->status == 0) {
                printf("%.*s\n", (int)resp->dataSize, resp->data);
            } else {
                fprintf(stderr, "%.*s\n", (int)resp->dataSize, resp->data);
            }
            break;
        default:
            fprintf(stderr, "Unknown response type\n");
            return;
    }
}

int main(int argc, char **argv)
{
    int clientfd, port;
    char *host, cmd[MAXLINE]; //change du buf to cmd from past etapes

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

    // 1. le client se connect au serveur maitre
    // 2. le maitre repond avec un message port
    // 3. le client ferme la connexion avec la maitre et se connect a l'esclave indique
    // 4. client envoie ses requets a l'esclave

    response_t resp;
    clientfd = Open_clientfd(host, port);
    Rio_readn(clientfd, &resp, sizeof(response_t)); // read welcome message from server
    ntoh_resp(&resp);
    
    if (resp.type == PORT) {
        int slave_port = resp.status; // port du serveur esclave à utiliser pour les requêtes suivantes
        char slave_host[MAXLINE];
        strncpy(slave_host, resp.data, MAXLINE);

        Close(clientfd); // on ferme la connexion avec le serveur maitre

        if (slave_host[0] == '\0') {
            fprintf(stderr, "Error: Empty IP address received from master server.\n");
            // on met l'ip du maitre
            strncpy(slave_host, host, MAXCHAR);
        }

        clientfd = Open_clientfd(slave_host, slave_port); // on se connecte au serveur esclave
        printf("Connected to slave server on %s:%d\n", slave_host, slave_port);
    } else {
        fprintf(stderr, "Unexpected response from server. Expected PORT message.\n");
        Close(clientfd);
        exit(1);
    }
    
    /*
     * At this stage, the connection is established between the client
     * and the server OS ... but it is possible that the server application
     * has not yet called "Accept" for this connection
     */
    printf("client connected to server OS\n"); 

    // Send request to server
    request_t req;

    while (Fgets(cmd, MAXLINE, stdin) != NULL) {

        // Les anciennes comparaisons appelaient strtok plusieurs fois sur cmd
        // ce qui avancait le pointeur interne et rendait les tokens suivants inaccessibles.
        // On copie cmd avant de tokeniser pour pouvoir relire les arguments apres.
        char cmd_copy[MAXLINE];
        strncpy(cmd_copy, cmd, MAXLINE);
        char *verb = strtok(cmd_copy, " \n");
        if (verb == NULL) continue;

        if (strcmp(verb, "get") == 0) {

            req.type = GET;
            strncpy(req.filename, strtok(NULL, " \n"), MAXNAME);
            hton_req(&req);
            // Check file offset du fichier

            char cmd[MAXLINE] = "cat repClient/";
            strcat(cmd, req.filename);
            strcat(cmd, " | wc -c > repClient/temp.txt");
            int status = system(cmd);
            if (status == 0) {
                int tempfd = Open("repClient/temp.txt", O_RDONLY | O_CREAT, 777);
                char maxOffsetStr[MAXLINE];
                Rio_readn(tempfd, &maxOffsetStr, sizeof(maxOffsetStr)); 
                req.offset = (uint32_t)strtoul(maxOffsetStr, NULL, 10);
                Close(tempfd);
                system("rm repClient/temp.txt");
            } else {
                printf("File does not exist or error occurred while checking offset.\n");
                req.offset = 0; // si le fichier n'existe pas encore, on commence à transférer depuis le début
            }

            printf("Sending GET request to server for file '%s' with offset %u\n", req.filename, req.offset);

            req.offset = htonl(req.offset); // on convertit l'offset en format réseau

            Rio_writen(clientfd, &req, sizeof(request_t));

            // Recevoir les blocs de reponse
            response_t resp;

            if (Rio_readn(clientfd, &resp, sizeof(response_t)) > 0) {
                ntoh_resp(&resp);
                if (req.offset == 0) {
                    handle_response(&resp, &req, true);
                } else {
                    handle_response(&resp, &req, false);
                }
                while (!resp.endOfFile) {
                    if (Rio_readn(clientfd, &resp, sizeof(response_t)) <= 0) {
                        fprintf(stdout, "Serveur a fermé la connexion\n");
                        break;
                    }
                    Sleep(1);
                    ntoh_resp(&resp);
                    handle_response(&resp, &req, false); // on ecrit dans le même fichier, en concaténant les données
                }
                req.offset = 0;
                fprintf(stdout, "End of file received\n");
                //break;
            } else { /* the server has prematurely closed the connection */
                fprintf(stderr, "Serveur a fermé la connexion\n");
                break;
            }

        } else if (strcmp(verb, "put") == 0) {

            char *filename = strtok(NULL, " \n");
            if (!filename) { fprintf(stderr, "usage: put <filename>\n"); continue; }
            
            // creer descripteur pour le fichier local de client
            char localpath[MAXLINE] = "repClient/";
            strcat(localpath, filename);
            int filefd = open(localpath, O_RDONLY);
            if (filefd < 0) {
                fprintf(stderr, "Erreur: fichier local '%s' introuvable\n", localpath);
                continue;
            }

            // envoie le request au server
            memset(&req, 0, sizeof(request_t));
            req.type = PUT;
            req.offset = 0;
            strncpy(req.filename, filename, MAXNAME);
            hton_req(&req);
            Rio_writen(clientfd, &req, sizeof(request_t));

            // envoie du fichier bloc par bloc sous forme de response_t
            // on le reutilise pour y mettre les donnees tant que c'est pas une response de serveur
            char filebuf[MAXCHAR];
            ssize_t nread;
            while ((nread = rio_readn(filefd, filebuf, MAXCHAR)) > 0) {
                response_t bloc;
                memset(&bloc, 0, sizeof(response_t));
                bloc.type = PUT;
                bloc.status = 0;
                bloc.endOfFile = (nread < MAXCHAR);
                bloc.dataSize = nread;
                memcpy(bloc.data, filebuf, nread);
                hton_resp(&bloc);
                Rio_writen(clientfd, &bloc, sizeof(response_t));
                if (nread < MAXCHAR) break;
            }

            //si le fichier est exactement n*MAXCHAR, envoie du dernier bloc
            if (nread == MAXCHAR) {
                response_t bloc;
                memset(&bloc, 0, sizeof(response_t));
                bloc.type = PUT;
                bloc.endOfFile = true;
                // data deja mis dans le bloc
                hton_resp(&bloc);
                Rio_writen(clientfd, &bloc, sizeof(response_t));
            }
            Close(filefd);

            //Attendre la reponse du serveur
            response_t resp;
            if (Rio_readn(clientfd, &resp, sizeof(response_t)) > 0) {
                ntoh_resp(&resp);
                handle_response(&resp, &req, true);
            }

        } else if (strcmp(verb, "ls") == 0) {

            memset(&req, 0, sizeof(request_t));
            req.type = LS;
            hton_req(&req);
            Rio_writen(clientfd, &req, sizeof(request_t));

            response_t resp;
            while (Rio_readn(clientfd, &resp, sizeof(response_t)) > 0) {
                ntoh_resp(&resp);
                handle_response(&resp, &req, false);
                if (resp.endOfFile) break;
            }

        } else if (strcmp(verb, "rm") == 0) {
            // ce qui ne matchait jamais. On utilise desormais verb (premier token).

            char *filename = strtok(NULL, " \n");
            if (!filename) { fprintf(stderr, "usage: rm <filename>\n"); continue; }

            memset(&req, 0, sizeof(request_t));
            req.type = RM;
            strncpy(req.filename, filename, MAXNAME);
            hton_req(&req);
            Rio_writen(clientfd, &req, sizeof(request_t));

            response_t resp;
            if (Rio_readn(clientfd, &resp, sizeof(response_t)) > 0) {
                ntoh_resp(&resp);
                handle_response(&resp, &req, true);
            }

        } else if (strcmp(verb, "login") == 0) {
            // jamais car cmd contenait "login user pass\n". On utilise desormais verb.
          
            char *login = strtok(NULL, " \n");
            char *pass = strtok(NULL, " \n");
            if (!login || !pass) {
                fprintf(stderr, "usage: login <login> <password>\n");
                continue;
            }

            memset(&req, 0, sizeof(request_t));
            req.type = LOGIN;
            // encodage "login:password" dans req.filename
            snprintf(req.filename, MAXNAME, "%s:%s", login, pass);
            hton_req(&req);
            Rio_writen(clientfd, &req, sizeof(request_t));

            response_t resp;
            if (Rio_readn(clientfd, &resp, sizeof(response_t)) > 0) {
                ntoh_resp(&resp);
                handle_response(&resp, &req, true);
            }

        } else if (strcmp(verb, "bye") == 0) {

            printf("Sending BYE request to server and closing connection.\n");
            memset(&req, 0, sizeof(request_t));
            req.type = BYE;
            hton_req(&req);
            Rio_writen(clientfd, &req, sizeof(request_t));
            break;

        } else {
            fprintf(stderr, "Invalid command. Use 'get', 'put', 'ls', 'rm', 'login' or 'bye'.\n");
            continue;
        }
    }
    Close(clientfd);
    exit(0);
}