#include "csapp.h"
#include "types.h"

#define NPROC 5

void handler(int sig);

pid_t pool[NPROC];


int apply_request(int connfd)
{
    size_t n;
    request_t req;
    response_t resp;

    //changed from while to if
    while ((n = Rio_readn(connfd, &req, sizeof(request_t))) != 0) {
        ntoh_req(&req);
        switch(req.type) {
            case GET:
                printf("Handling GET request for file: %s\n", req.filename);
                char path[MAXLINE] = "./repServeur/";
                strcat(path, req.filename);
                int fd = Open(path, O_RDONLY, 0);
                write(STDOUT_FILENO, "Opened file descriptor:\n", 24);
                char filebuf[MAXCHAR];

                // Transfert du fichier par blocs de MAXCHAR octets
                do {
                    resp.dataSize = Rio_readn(fd, filebuf, MAXCHAR);
                    if (resp.dataSize >= 0) {
                        resp.status = 0; // success
                        resp.endOfFile = false;
                        memcpy(resp.data, filebuf, resp.dataSize);
                    } else {
                        resp.status = -1; // error
                        memcpy(resp.data, "Erreur: le fichier n'existe pas", 32);
                    }
                    //printf("Sending response to client (status: %d, dataSize: %zd)\n", resp.status, resp.dataSize);
                    hton_resp(&resp);
                    Rio_writen(connfd, &resp, sizeof(response_t));

                } while ((resp.dataSize = Rio_readn(fd, filebuf, MAXCHAR)) == MAXCHAR); // on continue tant qu'on peut lire des données du fichier

                resp.endOfFile = true; // on indique que c'est la fin du fichier
                hton_resp(&resp);
                Rio_writen(connfd, &resp, sizeof(response_t)); // on envoie la réponse finale pour indiquer la fin du transfert

            
                Close(fd);
                break;
            case PUT:
                printf("Handling PUT request for file: %s\n", req.filename);
                break;
            case LS:
                printf("Handling LS request\n");
                break;
            case BYE:
                printf("Handling BYE request. Closing connection.\n");
                return 1; // on quitte la fonction pour fermer la connexion
            default:
                fprintf(stderr, "Unknown request type\n"); 
                resp.status = -1; // error
                memcpy(resp.data, "Erreur: Type de requête inconnu", 33);
                hton_resp(&resp);
                Rio_writen(connfd, &resp, sizeof(response_t));
        }
    }
    
    return 0;
}

//Gestionnaire de signal pour fermer proprement le pool
void handler(int sig) {
    printf("\nArrêt du serveur. Fermeture des processus fils...\n");
    for (int i = 0; i < NPROC; i++) {
        kill(pool[i], SIGTERM);
    }
    exit(0);
}

int main(int argc, char **argv)
{
    int listenfd, port;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    int pid;

    port = 2121;

    listenfd = Open_listenfd(port);
    clientlen = (socklen_t)sizeof(clientaddr);

    Signal(SIGINT, handler);

    printf("Démarrage du pool de %d processus sur le port %d...\n", NPROC, port);

    for (int i = 0; i < NPROC; i++) {
        if ((pid = Fork()) == 0) { //Fils
            while (1) {
                // sockaddr = SA
                int connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
                
                // Affichage du port pour l'expérimentation
                printf("[Fils %d] Requête reçue sur le port local %d\n", getpid(), ntohs(clientaddr.sin_port));
                
                int status = apply_request(connfd);
                Close(connfd);

                if (status == 1) { // si le client a envoyé une requête BYE, on ferme la connexion
                    printf("[Fils %d] Client déconnecté, remise en attente.\n", getpid());
                    continue; // on retourne à l'attente de nouvelles connexions
                } else {
                    printf("[Fils %d] Travail terminé, remise en attente.\n", getpid());
                }
            }
        } else { //Pere
            pool[i] = pid;
        }
    }

    // Le père ne fait plus rien à part attendre la fin (Ctrl+C)
    while (1) {
        pause(); 
    }

    return 0;
}



