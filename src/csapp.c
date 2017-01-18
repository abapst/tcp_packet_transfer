#include "csapp.h"

/* Reentrant, protocol independent helper to establish connection with a 
 * server. 
 */
int open_clientfd(char *hostname, char *port, struct sockaddr *sa)
{
  int clientfd;
  struct addrinfo hints, *listp, *p;

  /* Get a list of potential server address */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM; /* Open a connection */
  hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg */
  hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */
  Getaddrinfo(hostname, port, &hints, &listp);

  /* Walk the list for one that we can successfully connect to */
  for (p = listp; p; p = p->ai_next) {
    /* Create a socket descriptor */
    if ((clientfd = socket(p->ai_family,\
                 p->ai_socktype,\
                 p->ai_protocol)) < 0)
      continue; /* Socket failed, try the next */
    
    /* Connect to the server */
    if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
      *sa = *p->ai_addr; /* populate sockaddr */
      break; /* Success */
    Close(clientfd); /* Connect failed, try another */
  }

  /* Clean up */
  Freeaddrinfo(listp);
  if (!p) /* All connects failed */
    return -1;
  else  /* The last connect succeeded */
    return clientfd;
}

/* Reentrant, protocol independent helper that opens and returns a
 * listening descriptor.
 */
int open_listenfd(char *port)
{
  struct addrinfo hints, *listp, *p;
  int listenfd, optval=1;

  /* Get a list of potential server addresses */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;       /* Accept connections */
  hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
  hints.ai_flags |= AI_NUMERICSERV;      /* ... using port number */
  Getaddrinfo(NULL, port, &hints, &listp);
  
  /* Walk the list for one that we can bind to */
  for (p = listp; p; p = p->ai_next) {
    /* Create a socket descriptor */
    if ((listenfd = socket(p->ai_family,\
                 p->ai_socktype,\
                 p->ai_protocol)) < 0)
      continue; /* Socket failed, try the next */
    
    /* Eliminates "Address already in use" error from bind */
    Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
      (const void *)&optval, sizeof(int));
    
    /* Bind the descriptor to the address */
    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
      break; /* Success */
    Close(listenfd); /* Bind failed, try the next */
  }

  /* Clean up */
  Freeaddrinfo(listp);
  if (!p) /* No address worked */
    return -1;
  
  /* Make it a listening socket ready to accept connection requests */
  if (listen(listenfd, LISTENQ) < 0) {
    Close(listenfd);
    return -1;
  }
  return listenfd;
}

/************************************************
 * Wrappers for Pthreads thread control functions
*************************************************/

void Pthread_create(pthread_t *tidp, pthread_attr_t *attrp,
                    void * (*routine)(void *), void *argp)
{
  int rc;
  
  if ((rc = pthread_create(tidp, attrp, routine, argp)) != 0)
  posix_error(rc, "Pthread_create error");
}

void Pthread_detach(pthread_t tid)
{
  int rc;

  if ((rc = pthread_detach(tid)) != 0)
  posix_error(rc, "Pthread_detach error");
}

void Pthread_exit(void *retval)
{
  pthread_exit(retval);
}

pthread_t Pthread_self(void)
{
  return pthread_self();
}

/*************************************
 * Wrappers for Unix signal functions
 *************************************/

handler_t *Signal(int signum , handler_t *handler)
{
  struct sigaction action, old_action;

  action.sa_handler = handler;
  sigemptyset(&action.sa_mask); /* Block sigs of type being handled */ 
  action.sa_flags = SA_RESTART; /* Restart syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0)
  unix_error("Signal error");
  return (old_action.sa_handler);
}

/****************************************************
 * Wrappers for dynamic storage allocation functions
 ****************************************************/

void *Malloc(size_t size)
{
  void *p;

  if ((p = malloc(size)) == NULL)
  unix_error("Malloc error");
  return p;
}

void Free(void *ptr)
{
  free(ptr);
}

/********************************
 * Wrappers for Posix semaphores
 ********************************/

void Sem_init(sem_t *sem, int pshared, unsigned int value)
{
  if (sem_init(sem, pshared, value) < 0)
  unix_error("Sem_init error");
}

/********************
 * Robust I/O Helpers
*********************/

ssize_t rio_writen(int fd, void *usrbuf, size_t n)
{
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nwritten = write(fd, bufp, nleft)) <= 0) {
      if (errno == EINTR) /* interrupted by sig handler return */
        nwritten = 0;  /* and call write() again */
      else
        return -1;   /* errno set by write() */
    }
    nleft -= nwritten;
    bufp += nwritten;
  }
  return n;
}

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
  int cnt;
  
  while (rp->rio_cnt <= 0) { /* refill if buf is empty */
    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
    if (rp->rio_cnt < 0) {
      if (errno != EINTR) /* interrupted by sig handler return */
        return -1;
    } 
    else if (rp ->rio_cnt == 0) /* EOF */
      return 0;
    else
      rp->rio_bufptr = rp->rio_buf; /* reset buffer ptr */
  }

  /* Copy min(n,rp->rio_cnt) bytes from internal buf to usr buf */
  cnt = n;
  if (rp->rio_cnt < n)
    cnt = rp->rio_cnt;
  memcpy(usrbuf, rp->rio_bufptr, cnt);
  rp->rio_bufptr += cnt;
  rp->rio_cnt -= cnt;
  return cnt;
}

void rio_readinitb(rio_t *rp, int fd)
{
  rp->rio_fd = fd;
  rp->rio_cnt = 0;
  rp->rio_bufptr = rp->rio_buf;
}

/*
 * rio_readlineb - robustly read a text line (buffered)
 */
/* $begin rio_readlineb */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
  int n, rc;
  char c, *bufp = usrbuf;

  for (n = 1; n < maxlen; n++) {
    if ((rc = rio_read(rp, &c, 1)) == 1) {
      *bufp++ = c;
      if (c == '\n')
        break;
    } else if (rc == 0) {
      if (n == 1)
        return 0; /* EOF, no data read */
      else
        break;  /* EOF, some data was read */
    } else
      return -1; /* error */
  }
  *bufp = 0;
  return n;
}
/* $end rio_readlineb */


/****************
 * Wrappers
*****************/
void Setsockopt(int s, int level, int optname, const void *optval, int optlen)
{
  int rc;

  if ((rc = setsockopt(s,level,optname,optval,optlen)) < 0)
    unix_error("Setsockopt error");
} 

int Open_clientfd(char *hostname, char *port, struct sockaddr *sa)
{
  int rc;

  if ((rc = open_clientfd(hostname,port,sa)) < 0)
    unix_error("Open_clientfd error");
  return rc;
}

int Open_listenfd(char *port)
{
  int rc;

  if ((rc = open_listenfd(port)) < 0)
    unix_error("Open_listenfd error");
  return rc;
}

void Getaddrinfo( const char *node, const char *service,
                  const struct addrinfo *hints, struct addrinfo **res)
{
  int rc;
  
  if ((rc = getaddrinfo(node,service,hints,res)) != 0)
    gai_error(rc, "Getaddrinfo error");
}

void Freeaddrinfo(struct addrinfo *res)
{
  freeaddrinfo(res);
}

void Getnameinfo(const struct sockaddr *sa, socklen_t salen,
                 char *host, size_t hostlen,
                 char *service, size_t servlen, int flags)
{
  int rc;

  if ((rc = getnameinfo(sa, salen, host, 
              hostlen, service, servlen, flags)) != 0)
    unix_error("Getnameinfo error");
}

int Accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
  int rc;

  if ((rc = accept(s,addr,addrlen)) < 0)
    unix_error("Accept error");
  return rc;
}

void Close(int fd)
{
  int rc;
  if ((rc = close(fd)) < 0)
    unix_error("Close error");
}

void Fputs(const char *ptr, FILE *stream)
{
  if (fputs(ptr, stream) == EOF)
    unix_error("Fputs error");
}

/*
ssize_t Rio_readn(int fd, void *ptr, size_t nbytes) {
  ssize_t n;

  if ((n = rio_readn(fd, ptr, nbytes)) < 0)
    unix_error("Rio_readn error");
  return n;
}
*/

void Rio_writen(int fd, void *usrbuf, size_t n)
{
  if (rio_writen(fd, usrbuf, n) != n)
    unix_error("Rio_writen error");
}

void Rio_readinitb(rio_t *rp, int fd) {
  rio_readinitb(rp, fd);
}

ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
  ssize_t rc;

  if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    unix_error("Rio_readlineb error");
  return rc;
}

ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n)
{
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nread = rio_read(rp, bufp, nleft)) < 0)
      return -1;
    else if (nread == 0)
      break;
    nleft -= nread;
    bufp += nread;
  }
  return (n-nleft);
}

char *Fgets(char *ptr, int n, FILE *stream)
{
  char *rptr;

  if (((rptr = fgets(ptr,n,stream)) == NULL) && ferror(stream))
    app_error("Fgets error");
  
  return rptr;
}

/**************************
 * Error-handling functions
***************************/
void unix_error(char *msg) /* Unix-style error */
{
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(0); 
}

void posix_error(int code, char *msg) /* Posix-style error */
{
  fprintf(stderr, "%s: %s\n", msg, strerror(code));
  exit(0);
}

void app_error(char *msg) /* Application error */
{
  fprintf(stderr, "%s\n", msg);
  exit(0);
}

void gai_error(int code, char *msg) /* Getaddressinfo error */
{
  fprintf(stderr, "%s: %s\n",msg,gai_strerror(code));
  exit(0);
}
