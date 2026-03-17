#include "csapp.h"

#define NPROC 5

void echo(int connfd);
void handler(int sig);

pid_t children[NPROC];

int main(int argc, char **argv)
{
    int listenfd, port;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    int pid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);

    listenfd = Open_listenfd(port);
    clientlen = (socklen_t)sizeof(clientaddr);

    // Installation du gestionnaire de signal pour tuer les fils à l'arrêt
    Signal(SIGINT, handler);

    printf("Démarrage du pool de %d processus sur le port %d...\n", NPROC, port);

    for (int i = 0; i < NPROC; i++) {
        if ((pid = Fork()) == 0) {
            /* --- CODE DES EXÉCUTANTS (FILS) --- */
            while (1) {
                // Tous les fils bloquent ici. Le noyau en choisira un seul.
                int connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
                
                // Affichage du port pour l'expérimentation
                printf("[Fils %d] Requête reçue sur le port local %d\n", getpid(), ntohs(clientaddr.sin_port));
                
                echo(connfd);
                Close(connfd);
                printf("[Fils %d] Travail terminé, remise en attente.\n", getpid());
            }
        } else {
            /* --- CODE DU VEILLEUR (PÈRE) --- */
            children[i] = pid;
        }
    }

    // Le père ne fait plus rien à part attendre la fin (Ctrl+C)
    while (1) {
        pause(); 
    }

    return 0;
}

/**
 * Gestionnaire de signal pour fermer proprement le pool
 */
void handler(int sig) {
    printf("\n[Veilleur] Arrêt du serveur. Fermeture des processus fils...\n");
    for (int i = 0; i < NPROC; i++) {
        kill(children[i], SIGTERM);
    }
    exit(0);
}


