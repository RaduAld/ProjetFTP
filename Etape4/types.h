#define MAXNAME 100
#define MAXCHAR 100

#include <stdbool.h>

//nombre max d'esclaves 
#define MAX_SLAVES 16

// infos esclave pair (propagation des commandes Q16)
typedef struct slave_info_t {
    char host[MAXNAME];
    int port;
} slave_info_t;

typedef enum typereq_t {
    GET,
    PUT,
    LS,
    RM,
    PORT,
    SYNC,
    LOGIN,
    BYE
} typereq_t;

typedef struct request_t {
    typereq_t type;
    char filename[MAXLINE];
    // offset pour continuer un transfert de fichier à partir d'une certaine position (pour les GET et PUT)
    uint32_t offset; 
} request_t;

typedef struct response_t {
    typereq_t type;
    int status; // 0 for success, -1 for error
    bool endOfFile; // true if this is the last block of the file
    char data[MAXCHAR]; // pour PORT: adresse IP de l'esclave
    ssize_t dataSize;
} response_t;

// message envoye par le maitre a chaque esclave pour communiquer la liste des esclaves pairs
typedef struct slave_list_t {
    int        count;
    slave_info_t slaves[MAX_SLAVES];
} slave_list_t;
 
void ntoh_req(request_t *req) {
    req->type = ntohs(req->type);
    req->offset = ntohl(req->offset);
}

void hton_req(request_t *req) {
    req->type = htons(req->type);
    req->offset = htonl(req->offset);
}

void ntoh_resp(response_t *resp) {
    resp->type = ntohs(resp->type);
    resp->status = ntohl(resp->status);
    resp->dataSize = ntohl(resp->dataSize);
}

void hton_resp(response_t *resp) {
    resp->type = htons(resp->type);
    resp->status = htonl(resp->status);
    resp->dataSize = htonl(resp->dataSize);
}
