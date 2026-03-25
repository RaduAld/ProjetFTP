#define MAXNAME 100
#define MAXCHAR 100

#include <stdbool.h>

typedef enum typereq_t {
    GET,
    PUT,
    LS,
    BYE
} typereq_t;

typedef struct request_t {
    typereq_t type;
    char filename[MAXLINE];
    uint32_t offset; // offset pour continuer un transfert de fichier à partir d'une certaine position (pour les GET et PUT)
} request_t;

typedef struct response_t {
    typereq_t type;
    int status; // 0 for success, -1 for error
    bool endOfFile; // true if this is the last block of the file
    char data[MAXCHAR];
    ssize_t dataSize;
} response_t;

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
