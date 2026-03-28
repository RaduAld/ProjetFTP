#include "csapp.h"
#include "types.h"

// Serveur esclave
// Usage : ./esclave <master_host> <master_reg_port> <my_port>

// returns:
//      0 - connexion terminee normalement (client a ferme la connexion)
//      1 - client a envoye BYE
//     -1 - client deconnecte inattendue (mid-transfer)

// table de authentification
typedef struct {
    const char *login;
    const char *password;
} user_t;

static const user_t USERS[] = {
    {"admin", "1234"},
    {"alice", "passA"},
    {"radu", "passR"},
};
#define NB_USERS ((int)(sizeof(USERS) / sizeof(USERS[0])))

// liste des esclaves pairs recue du maitre pour propagation
static slave_list_t peers;
static int my_port_global;

// envoie une reponse simple et retourne ok (msg optionnel)
int send_simple(int connfd, typereq_t type, int status,
                bool eof, const char *msg) 
{
    response_t resp;
    memset(&resp, 0, sizeof(response_t));
    resp.type = type;
    resp.status = status;
    resp.endOfFile = eof;
    if (msg) {
        size_t len = strlen(msg);
        if (len >= MAXCHAR)
            len = MAXCHAR - 1;
        memcpy(resp.data, msg, len);
        resp.dataSize = (ssize_t)len;
    }
    hton_resp(&resp);
    if (rio_writen(connfd, &resp, sizeof(response_t)) < 0) {
        return -1;
    }
    return 0;
}

// propagation poour les operations PUT ou RM vers tous les autres esclaves
void propagate(typereq_t op, const char *filename,
                const char *filedata, ssize_t filesize) 
{
    for (int i = 0; i < peers.count; i++) {
        if (peers.slaves[i].port == my_port_global) continue; //exclure

        int fd = open_clientfd(peers.slaves[i].host, peers.slaves[i].port);
        if (fd < 0) {
            fprintf(stderr, "[Esclave] Propagation: impossible de joindre %s:%d\n",
                    peers.slaves[i].host, peers.slaves[i].port);
            continue;
        }

        // envoie une requete SYNC avec le type original encodee dans l'offset
        request_t sync_req;
        memset(&sync_req, 0, sizeof(request_t));
        sync_req.type = SYNC;
        sync_req.offset = (uint32_t)op;
        strncpy(sync_req.filename, filename, MAXLINE);
        hton_req(&sync_req);
        if (rio_writen(fd, &sync_req, sizeof(request_t)) < 0) {
            Close(fd); continue;
        }

        // Pour un PUT, envoyer le fichier bloc par bloc
        if (op == PUT && filedata && filesize > 0) {
            response_t bloc;
            memset(&bloc, 0, sizeof(response_t));
            bloc.type      = PUT;
            bloc.status    = 0;
            bloc.endOfFile = true;
            bloc.dataSize  = filesize;
            memcpy(bloc.data, filedata, filesize);
            hton_resp(&bloc);
            rio_writen(fd, &bloc, sizeof(response_t));
        }

        // attendre confirmation de l'esclave pair
        response_t conf;
        rio_readn(fd, &conf, sizeof(response_t));
        Close(fd);
    }
}

int apply_request(int connfd){
    size_t n;
    request_t req;
    response_t resp;
    bool auth = false;

    // changed from while to if
    // changed all instances of Rio to rio so it doesn't exit if error
    while ((n = rio_readn(connfd, &req, sizeof(request_t))) != 0) {

        if ((ssize_t)n < 0) {
            fprintf(stderr, "[Esclave] Erreur lecture requête (client déconnecté ?)\n");
            return -1;
        }

        ntoh_req(&req);

        switch(req.type) {
            case GET: {
                printf("Handling GET request for file: %s\n", req.filename);
                char path[MAXLINE] = "./repServeur/";
                strcat(path, req.filename);

                int fd = open(path, O_RDONLY);

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
            }
            case PUT: {
                printf("Handling PUT request for file: %s\n", req.filename);

                if (!auth) {
                    send_simple(connfd, PUT, -1, true,
                            "Erreur: authentification requise");
                    break;
                }

                char path[MAXLINE] = "./repServeur/";
                strcat(path, req.filename);

                int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    send_simple(connfd, PUT, -1, true,
                            "Erreur: impossible de créer le fichier");
                    break;
                }

                char accumulated[MAXCHAR * 64];
                ssize_t total = 0;
                int client_gone = 0;

                while (1) {
                    response_t bloc; 
                    // le client envoie des response_t pour les donnees

                    ssize_t nr = rio_readn(connfd, &bloc, sizeof(response_t));
                    if (nr <= 0) { client_gone = 1; break; }
                    ntoh_resp(&bloc);

                    if (bloc.dataSize > 0) {
                        rio_writen(fd, bloc.data, bloc.dataSize);
                        // accumuler pour la propagation (best-effort, taille limitee)
                        if (total + bloc.dataSize <= (ssize_t)sizeof(accumulated)) {
                            memcpy(accumulated + total, bloc.data, bloc.dataSize);
                            total += bloc.dataSize;
                        }
                    }
                    if (bloc.endOfFile) break;
                }

                Close(fd);

                if (client_gone) return -1;

                // propager aux autres esclaves
                propagate(PUT, req.filename, accumulated, total);

                send_simple(connfd, PUT, 0, true, "Fichier téléversé avec succès");
                break;
            }
            case LS: {
                printf("Handling LS request\n");
                FILE *fp = popen("ls ./repServeur/", "r");
                if (!fp) {
                    send_simple(connfd, LS, -1, true, "Erreur: Impossible de lister les fichiers");
                    break;
                }
                char lsbuf[MAXCHAR];
                bool first = true;
                int client_gone = 0;

                while (fgets(lsbuf, sizeof(lsbuf), fp) != NULL) {
                    (void)first;
                    memset(&resp, 0, sizeof(response_t));
                    resp.type = LS;
                    resp.status = 0;
                    resp.endOfFile = false;
                    resp.dataSize = (ssize_t)strlen(lsbuf);
                    memcpy(resp.data, lsbuf, resp.dataSize);
                    hton_resp(&resp);
                    if (rio_writen(connfd, &resp, sizeof(response_t)) < 0) {
                        fprintf(stderr, "[Esclave] Client déconnecté pendant LS, abandon du transfert.\n");
                        client_gone = 1;
                        break;
                    }
                    first = false;
                }
                pclose(fp);

                if(!client_gone) {
                    // marquer de fin
                    if (send_simple(connfd, LS, 0, true, "") < 0) {
                        return -1;
                    }
                }
                break;
            }
            case RM: {
                printf("Handling RM request for file: %s\n", req.filename);

                if(!auth) {
                    send_simple(connfd, RM, -1, true, "Erreur: authentification requise");
                    break;
                }

                char path[MAXLINE] = "./repServeur/";
                strcat(path, req.filename);

                if (unlink(path) < 0) {
                    send_simple(connfd, RM, -1, true,
                                "Erreur: fichier introuvable ou suppression impossible");
                    break;
                }
 
                // propager aux autres esclaves
                propagate(RM, req.filename, NULL, 0);
 
                send_simple(connfd, RM, 0, true, "Fichier supprimé avec succès");
                break;
            }
            case SYNC: {
                typereq_t op = (typereq_t)req.offset; // PUT ou RM dans offset

                if (op == RM) {
                    char path[MAXLINE] = "./repServeur/";
                    strcat(path, req.filename);
                    unlink(path);
                    send_simple(connfd, SYNC, 0, true, "ok");

                } else if (op == PUT) {
                    char path[MAXLINE] = "./repServeur/";
                    strcat(path, req.filename);
                    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    response_t bloc;
                    ssize_t nr = rio_readn(connfd, &bloc, sizeof(response_t));
                    if (nr > 0 && fd >= 0) {
                        ntoh_resp(&bloc);
                        if (bloc.dataSize > 0) 
                            rio_writen(fd, bloc.data, bloc.dataSize);
                    }
                    if (fd >= 0) Close(fd);
                    send_simple(connfd, SYNC, 0, true, "ok");
                }

                // SYNC termine tourjouts la connexion apres une operation
                return 0;
            }
            case LOGIN: {
                // La version avec guillemets doubles passe un const char* au lieu d'un char
                char *sep = strchr(req.filename, ':');
                if (!sep) {
                    send_simple(connfd, LOGIN, -1, true, "Erreur: format attendu login:password");
                    break;
                }
                *sep = '\0';
                char *login = req.filename;
                char *password = sep + 1;

                bool found = false;
                for (int i = 0; i < NB_USERS; i++) {
                    if (strcmp(USERS[i].login, login) == 0 && 
                        strcmp(USERS[i].password, password) == 0) {
                        found = true;
                        break;
                    }
                }

                if (found) {
                    auth = true;
                    printf("[Esclave] Utilisateur '%s' authentifié.\n", login);
                    send_simple(connfd, LOGIN, 0, true, "Authentification réussie");
                } else {
                    send_simple(connfd, LOGIN, -1, true,
                                "Erreur: login ou mot de passe incorrect");
                }
                break;
            }
            case BYE:
                printf("Handling BYE request. Closing connection.\n");
                return 1; // on quitte la fonction pour fermer la connexion

            default:
                fprintf(stderr, "Unknown request type\n"); 
                resp.status = -1; // error
                memcpy(resp.data, "Erreur: Type de requête inconnu", 33);
                hton_resp(&resp);
                rio_writen(connfd, &resp, sizeof(response_t));
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

    Signal(SIGPIPE, SIG_IGN);
 
    char *master_host    = argv[1];
    int   master_reg_port = atoi(argv[2]);
    my_port_global         = atoi(argv[3]);

    // ouvrir le port d'ecoute de cet esclave
    int listenfd = Open_listenfd(my_port_global);
    printf("[Esclave] Port d'écoute ouvert sur %d\n", my_port_global);

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
    reg_msg.status = my_port_global;
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

    // recevoir la liste des pairs depuis le maitre
    slave_list_t peers_net;
    Rio_readn(reg_fd, &peers_net, sizeof(slave_list_t));
    peers.count = ntohl(peers_net.count);
    for (int i = 0; i < peers.count; i++) {
        strncpy(peers.slaves[i].host, peers_net.slaves[i].host, MAXNAME);
        peers.slaves[i].port = ntohl(peers_net.slaves[i].port);
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
               inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
 
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