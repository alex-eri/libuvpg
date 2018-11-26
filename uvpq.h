#include <uv.h>
#include <libpq-fe.h>
#include <stdlib.h>

#define MAX_CONNINFO_LENGTH 1048

typedef struct uvpq_connection uvpq_connection;
typedef struct uvpq_pool uvpq_pool;
typedef struct uvpq_request uvpq_request;

typedef void (*req_cb)(uvpq_request *request, PGresult* ressult);

typedef struct uvpq_request {

    const char* command;
    int nParams;
    const Oid *paramTypes;
    const char * const *paramValues;
    const int *paramLengths;
    const int *paramFormats;

  req_cb cb;
  void* data;
  uvpq_request * next;
} uvpq_request;

typedef struct uvpq_request uvpq_request;



uvpq_pool * uvpq_pool_create( uv_loop_t *loop, int size, char* uri);
uvpq_connection * uvpq_pool_acquire(uvpq_pool * pool);


void uvpq_connection_query(uvpq_connection * conn,
                    const char *command,
                    int nParams,
                    const Oid *paramTypes,
                    const char * const *paramValues,
                    const int *paramLengths,
                    const int *paramFormats,
                    req_cb cb,
                    void* data);
