
#define MAXCHAR 5000

typedef enum typereq_t {
    GET,
    PUT,
    LS
} typereq_t;

typedef struct request_t {
    typereq_t type;
    char filename[MAXLINE];
} request_t;

typedef struct response_t {
    typereq_t type;
    int status; // 0 for success, -1 for error
    char data[MAXCHAR];
    ssize_t dataSize;
} response_t;