#include "csapp.h"
#include "types.h"

// Serveur esclave
// Usage : ./esclave <master_host> <master_reg_port> <my_port>
//
// A demarer AVANT le client, mais APRES que le maître a ouvert son port
// d'enregistrement (SLAVE_REG_PORT).  L'esclave :
//   1. ouvre son propre port d'ecoute (my_port).
//   2. s'enregistre auprès du maître en lui envoyant un message PORT
//      contenant son port et son adresse IP.
//   3. attend l'accusé de réception du maître.
//   4. entre dans sa boucle principale et traite les requêtes des clients
//      (GET, PUT, LS, BYE) exactement comme dans les étapes précédentes.

// returns:
//      0 - connexion terminee normalement (client a ferme la connexion)
//      1 - client a envoye BYE
//     -1 - client deconnecte inattendue (mid-transfer)
int apply_request(int connfd){
    size_t n;
    request_t req;
    response_t resp;

    // changed from while to if
    // changed all instances of Rio to rio so it doesn't exit if error
    while ((n = rio_readn(connfd, &req, sizeof(request_t))) != 0) {

        if ((ssize_t)n < 0) {
            fprintf(stderr, "[Esclave] Erreur lecture requête (client déconnecté ?)\n");
            return -1;
        }

        ntoh_req(&req);
        switch(req.type) {
            case GET:
                printf("Handling GET request for file: %s\n", req.filename);
                char path[MAXLINE] = "./repServeur/";
                strcat(path, req.filename);

                int fd = Open(path, O_RDONLY, 0);

                if (fd < 0) {
                    // Fichier introuvable : on envoie une réponse d'erreur
                    memset(&resp, 0, sizeof(response_t));
                    resp.type      = GET;
                    resp.status    = -1;
                    resp.endOfFile = true;
                    memcpy(resp.data, "Erreur: le fichier n'existe pas", 32);
                    resp.dataSize  = 32;
                    hton_resp(&resp);
                    rio_writen(connfd, &resp, sizeof(response_t));
                    break;
                }

                //write(STDOUT_FILENO, "Opened file descriptor:\n", 24);
                char filebuf[MAXCHAR];

                // on se positionne à l'offset spécifié dans la requête
                lseek(fd, req.offset, SEEK_SET);

                ssize_t nread = rio_readn(fd, filebuf, MAXCHAR);
                int client_gone = 0;

                // Transfert du fichier par blocs de MAXCHAR octets
                do {
                    memset(&resp, 0, sizeof(response_t));
                    resp.type = GET;
                    if (nread >= 0) {
                        resp.status = 0; // success
                        resp.endOfFile = false;
                        resp.dataSize = nread;
                        memcpy(resp.data, filebuf, nread);
                    } else {
                        resp.status    = -1;
                        resp.endOfFile = true;
                        memcpy(resp.data, "Erreur: lecture fichier", 24);
                        resp.dataSize  = 24;
                    }
                    //printf("Sending response to client (status: %d, dataSize: %zd)\n", resp.status, resp.dataSize);
                    hton_resp(&resp);
                    
                    // si le client est parti, on retourne -1 au lieu de tuer le processus
                    if (rio_writen(connfd, &resp, sizeof(response_t)) < 0) {
                        fprintf(stderr, "[Esclave] Client déconnecté pendant GET, abandon du transfert.\n");
                        client_gone = 1;
                        break;
                    }

                    // on continue tant qu'on peut lire des données du fichier
                } while ((nread = rio_readn(fd, filebuf, MAXCHAR)) == MAXCHAR); 

                if (!client_gone) {
                     // Envoyer le dernier bloc (< MAXCHAR) ou le marqueur de fin
                    memset(&resp, 0, sizeof(response_t));
                    resp.type      = GET;
                    resp.status    = 0;
                    resp.endOfFile = true;
                    resp.dataSize  = (nread > 0) ? nread : 0;
                    if (nread > 0) memcpy(resp.data, filebuf, nread);
                    hton_resp(&resp);
 
                    if (rio_writen(connfd, &resp, sizeof(response_t)) < 0) {
                        fprintf(stderr, "[Esclave] Client déconnecté lors de l'envoi du dernier bloc.\n");
                        client_gone = 1;
                    }
                }

                Close(fd);

                if(client_gone) return -1;

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

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <master_host> <master_reg_port> <my_port>\n", argv[0]);
        exit(1);
    }
 
    char *master_host    = argv[1];
    int   master_reg_port = atoi(argv[2]);
    int   my_port         = atoi(argv[3]);

    // ouvrir le port d'ecoute de cet esclave
    int listenfd = Open_listenfd(my_port);
    printf("[Esclave] Port d'écoute ouvert sur %d\n", my_port);

    // l'esclave s'enregistre au maitre
    // il envoye un message PORT:
    //      resp.status = port d'ecoute de l'esclave
    //      resp.data = adresse IP que le client devra utiliser
    int reg_fd = Open_clientfd(master_host, master_reg_port);

    // determine l'adresse IP de l'esclave
    char my_host[MAXLINE];
    if (gethostname(my_host, sizeof(my_host)) != 0) {
        printf("[Esclave] Erreur lors de la récupération du nom d'hôte.\n");
        strncpy(my_host, master_host, MAXLINE);
    }

    response_t reg_msg;
    memset(&reg_msg, 0, sizeof(response_t)); //init at 0 ?
    reg_msg.type = PORT;
    reg_msg.status = my_port;
    strncpy(reg_msg.data, my_host, MAXCHAR);

    hton_resp(&reg_msg);
    Rio_writen(reg_fd, &reg_msg, sizeof(response_t));

    // attendre reponse de confirmation de maitre
    response_t conf_msg;
    Rio_readn(reg_fd, &conf_msg, sizeof(response_t));
    ntoh_resp(&conf_msg);

    if (conf_msg.type != PORT || conf_msg.status != 0) {
        fprintf(stderr, "[Esclave] Enregistrement refusé par le maître.\n");
        Close(reg_fd);
        exit(1);
    }
    Close(reg_fd);
    printf("[Esclave] Enregistré auprès du maître (%s:%d). En attente de clients...\n",
           master_host, master_reg_port);

    // Traitement de requetes de clients
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    clientlen = (socklen_t)sizeof(clientaddr);

    while (1) {
        int connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        printf("[Esclave] Connexion acceptée depuis %s:%d\n",
               inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port)); // checkAksjdhgaksudygfkasdiugfiagsfpia
 
        int status = apply_request(connfd);
        Close(connfd);

        if (status == 1) {
            printf("[Esclave] Client déconnecté (BYE), remise en attente.\n");
        } else if(status == -1) {
            printf("[Esclave] Client interrompu en cours de transfert, remise en attente.\n");
        } else {
            printf("[Esclave] Requête traitée, remise en attente.\n");
        }
    }


    return 0;
}