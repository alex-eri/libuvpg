#include "util.h"
#include "uvpq.h"
#include <string.h>



typedef struct queue_s {
    uvpq_request *head;
    uvpq_request *tail;
  } queue_t;

enum up_conn_state {
    UP_NEW = 0,
    UP_CONNECTING,
    UP_RESETTING,
    UP_CONNECTED,
    UP_BAD_CONNECTION,
    UP_BAD_RESET,
};

typedef struct uvpq_connection {
  PGconn* conn;
  enum up_conn_state state;
  int fd;
  uv_loop_t *loop;
  uv_poll_t poll;
  int eventmask;
  uvpq_connection* next;
  queue_t * pending;
  uvpq_request *live;

} uvpq_connection;

typedef struct uvpq_pool {
  uvpq_connection *head;
  uvpq_connection *tail;
  uv_loop_t *loop;
} uvpq_pool;



uvpq_request * uvpq_requests_pop(struct queue_s * rqs);
void poll_cb(uv_poll_t* handle, int status, int events);


static void update_poll_eventmask(uvpq_connection* conn, int eventmask)
{
    if(conn->eventmask != eventmask) {
        int r = uv_poll_start(&conn->poll, eventmask, poll_cb);
        if(r != 0)
            failwith("uv_poll_start: %s\n", uv_strerror(r));
        conn->eventmask = eventmask;
    }
}


void uvpq_pool_put(uvpq_pool * pool, uvpq_connection * conn)
{
    conn->loop = pool->loop;
    //printf("%p -> %p\n", pool->tail, conn);


    if (pool->head==NULL) {
      pool->head = conn;
      pool->tail = conn;
    } else {
      pool->tail->next = conn;
      pool->tail = pool->tail->next;
    }

}


void uvpq_PQsendQueryParams(uvpq_connection *conn){
        uvpq_request *rq = uvpq_requests_pop(conn->pending);
        conn->live = rq;
        if (rq) {

        if(!PQsendQueryParams(
                            conn->conn,
                            rq->command,
                            rq->nParams,
                            rq->paramTypes,
                            rq->paramValues,
                            rq->paramLengths,
                            rq->paramFormats,
                            1))
            failwith("PQsendQuery: %s\n", PQerrorMessage(conn->conn));
        }
}

void free_req(uvpq_request* req){
  free(req);
}

void poll_cb(uv_poll_t* handle, int status, int events) {

    if(status < 0)
        failwith("unexpected status %d\n", status);

    int r;
    uvpq_connection *conn = handle->data;
    int eventmask = conn->eventmask;

    if (events & UV_WRITABLE)
    {
      r = PQflush(conn->conn);
      if (r == 0) {
        eventmask &= ~UV_WRITABLE;
      }
      if (r == 0 && conn->live == NULL) {
        if (!PQisBusy(conn->conn)) {
          uvpq_PQsendQueryParams(conn);
        }
          PQflush(conn->conn);
          //printf("write %d\n", eventmask);
      }
    }
    if (events & UV_READABLE)
    {
      if(!PQconsumeInput(conn->conn))
            failwith("PQsendQuery: %s\n", PQerrorMessage(conn->conn));
      if (!PQisBusy(conn->conn)) {
        PGresult * res = PQgetResult(conn->conn);
        if (res) {
        req_cb cb = conn->live->cb;

          cb(conn->live, res);
          free_req(conn->live);
          conn->live = NULL;
          //printf("read %d\n", eventmask);
        }
      }
      if (conn->pending->head != NULL)
         eventmask |= UV_WRITABLE;

    }
    update_poll_eventmask(conn, eventmask);
}


void uvpq_connect(uvpq_pool * pool, uvpq_connection* conn, char* uri) {
  conn->state = UP_CONNECTING;
  conn->live = NULL;
  conn->next = NULL;

  conn->conn = PQconnectdb(uri);
  if(conn->conn == NULL)
    failwith("PQconnectdb failed");
  if(PQstatus(conn->conn) == CONNECTION_BAD)
    failwith("connection is bad");
  int fd = PQsocket(conn->conn);
  if(fd < 0)
    failwith("PQsocket failed");
  conn->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
  if(conn->fd < 0)
    failwith("unable to dup fd %d: %s\n", fd, uv_strerror(errno));

  if( PQsetnonblocking(conn->conn, 1) !=0)
    failwith("set nonblocking failed: %s", PQerrorMessage(conn->conn));

  int r;
  if((r = uv_poll_init(pool->loop, &conn->poll, conn->fd)) != 0)
        failwith("uv_poll_init: %s\n", uv_strerror(r));

  conn->poll.data = conn;


  conn->state = UP_CONNECTED;
  uvpq_pool_put(pool, conn);
  update_poll_eventmask(conn, UV_READABLE);

}



uvpq_pool * uvpq_pool_create( uv_loop_t *loop, int size, char* uri)
{
  uvpq_pool * pool=malloc (sizeof(uvpq_pool));
  pool->loop = loop;
  for (int i=0;i<size;i++){
    uvpq_connection * conn ;
    conn = malloc(sizeof(uvpq_connection));
    conn->pending = malloc (sizeof(queue_t));
    memset(conn->pending,0,sizeof(queue_t));
    uvpq_connect(pool, conn, uri);
  }
  return pool;
}

uvpq_connection * uvpq_pool_next(uvpq_pool * pool)
{
  uvpq_connection * chain;
  chain = pool->head;

  pool->head = chain->next;
  chain->next = pool->head;

  pool->tail->next = chain;
  pool->tail = chain;

  return chain;
}

uvpq_connection * uvpq_pool_acquire(uvpq_pool * pool)
{
  uvpq_connection * conn;
  do {
    conn = uvpq_pool_next(pool);
    if (conn && conn->state == UP_CONNECTED)
      return conn;
  } while(uv_run(pool->loop, UV_RUN_ONCE));
  return NULL;
}


void uvpq_requests_push(struct queue_s * rqs, uvpq_request *r)
{
    if (rqs->head==NULL) {
      rqs->head = r;
      rqs->tail = rqs->head;
    } else {
      rqs->tail->next = r;
      rqs->tail = rqs->tail->next;
    }
}

uvpq_request * uvpq_requests_pop(struct queue_s * rqs)
{
  uvpq_request *r;

   //printf("%p/n", rqs->head);

   if(rqs->head) {

    r = rqs->head;
    rqs->head = r->next;
    r->next=NULL;
    return r;
   }
  else {
    return NULL;
  }
}

void uvpq_connection_query(uvpq_connection * conn,
                    const char *command,
                    int nParams,
                    const Oid *paramTypes,
                    const char * const *paramValues,
                    const int *paramLengths,
                    const int *paramFormats,
                    req_cb cb,
                    void* data)
{
  uvpq_request * r = malloc(sizeof(uvpq_request));
  r->next = NULL;
  r->command=command;
  r->nParams=nParams;
  r->paramTypes=paramTypes;
  r->paramValues=paramValues;
  r->paramLengths=paramLengths;
  r->paramFormats=paramFormats;

  r->cb = cb;
  r->data = data;

  uvpq_requests_push(conn->pending,r);
  update_poll_eventmask(conn, UV_READABLE|UV_WRITABLE);
}
