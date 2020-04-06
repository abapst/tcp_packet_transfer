#ifndef __CSAPP_H__
#define __CSAPP_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Default file permissions are DEF_MODE & ~DEF_UMASK */
#define DEF_MODE    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH
#define DEF_UMASK   S_IWGRP|S_IWOTH

/* Simplifies calls to bind(), connect(), and accept() */
typedef struct sockaddr SA;

/* Persistent state for the robust I/O (Rio) package */
#define RIO_BUFSIZE 8192
typedef struct {
  int rio_fd;                /* Descriptor for this internal buf */
  int rio_cnt;               /* Unread bytes in internal buf */
  char *rio_bufptr;          /* Next unread byte in internal buf */
  char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

typedef void handler_t(int);

#define MAXLINE 8192 /* Max text line length */
#define MAXBUF  8192 /* Max I/O buffer size */
#define LISTENQ 1024 /* Second argument to listen() */

int open_clientfd(char *hostname, char *port, struct sockaddr *sa);
int Open_clientfd(char *hostname, char *port, struct sockaddr *sa);

int open_listenfd(char *port);
int Open_listenfd(char *port);

void Getaddrinfo(const char *node, const char *service,
                 const struct addrinfo *hints, struct addrinfo **res);

void Freeaddrinfo(struct addrinfo *res);

void Close(int fd);

void *Malloc(size_t size);
void Free(void *ptr);

void Sem_init(sem_t *sem, int pshared, unsigned int value);

handler_t *Signal(int signum, handler_t *handler);

char *Fgets(char *ptr, int n, FILE *stream);
void Fputs(const char *ptr, FILE *stream);

void Pthread_create(pthread_t *tidp, pthread_attr_t *attrp, void * (*routine)(void *), void *argp);
void Pthread_detach(pthread_t tid);
pthread_t Pthread_self(void);
void Pthread_exit(void *retval);

void rio_readinitb(rio_t *rp, int fd);
void Rio_readinitb(rio_t *rp, int fd);
ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n);

ssize_t rio_writen(int fd, void *usrbuf, size_t n);
void Rio_writen(int fd, void *usrbuf, size_t n);

ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

void Setsockopt(int s, int level, int optname, const void *optval, int optlen);

void Getnameinfo(const struct sockaddr *sa, socklen_t salen,
                 char *host, size_t hostlen,
                 char *service, size_t servlen, int flags);

int Accept(int s, struct sockaddr *addr, socklen_t *addrlen);

void unix_error(char *msg);
void posix_error(int code, char *msg);
void app_error(char *msg);
void gai_error(int code, char *msg);

#endif
