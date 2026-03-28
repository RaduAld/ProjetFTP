#include "csapp.h"
#include "types.h"

#define NPROC 5
#define NB_SLAVES 2
#define MASTER_PORT 2121
#define SLAVE_REG_PORT 2120 //pour l'enregistrement des esclaves
#define SLAVE_BASE_PORT 2122 // les esclaves écoutent sur 2122, 2123, ...

void handler(int sig);

pid_t pool[NPROC];

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
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    int pid;

    // infos pour chaque esclave : adresse IP + port
    char slave_ips[NB_SLAVES][MAXLINE];
    int slave_ports[NB_SLAVES];

    // Lancement automatique des esclaves via fork + port
    int reg_listenfd = Open_listenfd(SLAVE_REG_PORT);
    printf("Maître : en attente de %d esclave(s) sur le port %d...\n",
           NB_SLAVES, SLAVE_REG_PORT);
 
    for (int i = 0; i < NB_SLAVES; i++) {
        int slave_port = SLAVE_BASE_PORT + i;
 
        char slave_port_str[16];
        char reg_port_str[16];
        snprintf(slave_port_str, sizeof(slave_port_str), "%d", slave_port);
        snprintf(reg_port_str,   sizeof(reg_port_str),   "%d", SLAVE_REG_PORT);
 
        pid_t spid = Fork();
        if (spid == 0) { // Fils : devient le processus esclave
            // argv attendu par esclave : ./esclave <master_host> <master_reg_port> <my_port>
            char *args[] = { "./esclave", "localhost", reg_port_str, slave_port_str, NULL };
            execv(args[0], args);
            // Execv ne retourne jamais en cas de succes
            fprintf(stderr, "Erreur: impossible de lancer ./esclave\n");
            exit(1);
        }
    }
 
    // ------------------------------------------------------------------
    // Phase d'enregistrement : attendre que chaque esclave se connecte
    // ------------------------------------------------------------------
    for (int i = 0; i < NB_SLAVES; i++) {
        clientlen = (socklen_t)sizeof(clientaddr);
        int reg_connfd = Accept(reg_listenfd, (SA *)&clientaddr, &clientlen);
 
        // l'esclave envoie un message PORT
        response_t reg_resp;
        Rio_readn(reg_connfd, &reg_resp, sizeof(response_t));
        ntoh_resp(&reg_resp);
 
        if (reg_resp.type != PORT) {
            fprintf(stderr, "Maître : message d'enregistrement inattendu de l'esclave %d\n", i);
            Close(reg_connfd);
            exit(1);
        }
 
        slave_ports[i] = reg_resp.status;
        strncpy(slave_ips[i], reg_resp.data, MAXLINE);
        printf("Maître : esclave %d enregistré — %s:%d\n",
               i, slave_ips[i], slave_ports[i]);
 
        // confirmation a l'esclave
        response_t conf_msg;
        memset(&conf_msg, 0, sizeof(response_t));
        conf_msg.type   = PORT;
        conf_msg.status = 0;
        hton_resp(&conf_msg);
        Rio_writen(reg_connfd, &conf_msg, sizeof(response_t));
 
        Close(reg_connfd);
    }
 
    Close(reg_listenfd);
    printf("Maître : tous les esclaves sont enregistrés. Prêt à accepter des clients.\n");
 
    // Gestion de clients 
    listenfd = Open_listenfd(MASTER_PORT);
    clientlen = (socklen_t)sizeof(clientaddr);

    Signal(SIGINT, handler);
    
    int tourniquet = 0; // pour faire du round-robin entre les esclaves

    printf("Démarrage du pool de %d processus sur le port %d...\n", NPROC, MASTER_PORT);

    for (int i = 0; i < NPROC; i++) {
        if ((pid = Fork()) == 0) { //Fils
            while (1) {
                // sockaddr = SA
                int connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

                // choisir un esclave
                int chosen = tourniquet;
                tourniquet = (tourniquet + 1) % NB_SLAVES;

                response_t resp;
                memset(&resp, 0, sizeof(response_t));
                resp.type = PORT;
                resp.status = slave_ports[chosen];
                strncpy(resp.data, slave_ips[chosen], MAXCHAR);

                hton_resp(&resp);
                Rio_writen(connfd, &resp, sizeof(response_t));
                Close(connfd); // fin de la phase maitre pour le client

                printf("[Fils %d] Client redirigé vers esclave %d (%s:%d)\n",
                       getpid(), chosen, slave_ips[chosen], slave_ports[chosen]);
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



