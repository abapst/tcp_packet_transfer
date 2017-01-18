/***************************************************************************
 * Server routine simulates processing of large image data packets from a
 * client. It implements a thread-safe ring buffer that can be accessed by 
 * multiple client read threads. When a client connects, the server starts
 * a new read thread that reads data packets into the ring buffer. If the
 * buffer is currently full, it blocks until a spot opens up. Processing is
 * handled by a concurrent processing thread that continuously reads and
 * processes packet items from the ring buffer,and blocks if the buffer is
 * empty. The server closes the read thread when it receives a message from
 * the client indicating that all packets have been sent. The processing
 * thread does not exit until a SIGINT (ctrl-c) has been received, which
 * exits the server program and frees the ring buffer memory.
 *
 * Author: Aleksander Bapst
 * ************************************************************************/

#include "ring_buffer.h"

/* Global pointer to ring buffer */
ring_buffer *buf = NULL;

/* Mutex protecting the number of active client threads */
int nclients = 0;
pthread_mutex_t nclients_lock;

int verbose = 0;
int use_checksum = 0;

/* Function declarations */
void *client_job(void *varargp);
void *buffer_job();
void close_openfds(int *clientfd, int *serverfd);
void sigint_handler(int sig);
void print_usage();

int main(int argc, char **argv)
{
  int listenfd, *connfdp, opt, n_buf_items = DEFAULT_BUFFER_SIZE;
  float packet_size = (float)sizeof(buf_item); // packet_size in bytes
  char *port;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; /* Enough space for any address */

  pthread_t tid[MAX_CLIENTS]; // Thread id
  pthread_t tid_job; // Job processing thread

  pthread_mutex_init(&nclients_lock,NULL);

  Signal(SIGINT, sigint_handler); /* ctrl-c */

  char client_hostname[MAXLINE], client_port[MAXLINE];

  if (argc < 2) {
    print_usage();
        exit(0);
    }

  /* Required args */
  port = argv[1];

  /* Parse optional args */
  while ((opt = getopt(argc, argv, "n:chv")) != -1) {
    switch(opt) {
      case 'n':
        n_buf_items = atoi(optarg);
        break;
      case 'c':
        use_checksum = 1;
        break;
      case 'h':
        print_usage();
        exit(0);
      case 'v':
        verbose = 1;
        break;
      default:
        print_usage();
        continue;
    } 
  }

  /* Initialize ring buffer */
  buf = init_buf(n_buf_items);

  /* Processing thread that grabs items from ring
   * buffer as it gets filled */
  Pthread_create(&tid_job, NULL, buffer_job, NULL);

  /* Listen for client requests and create new threads when they arrive */
  listenfd = Open_listenfd(port);

  printf("----------------------------------------------------------------\n");
  printf("Image processing server started, listening on port %s\n", port);
  printf("Server buffer capacity: %d packets\n",n_buf_items);
  printf("Total buffer size: %.2f MB\n",n_buf_items*packet_size/(1<<20));
  if (verbose)
    printf("[verbose mode]");
  if (use_checksum)
    printf("[Using MD5 checksum]");
  if (verbose || use_checksum)
    printf("\n");
  printf("----------------------------------------------------------------\n");

  while(nclients < MAX_CLIENTS) {
    clientlen = sizeof(struct sockaddr_storage);

    // Allocate file descriptor to avoid race between threads and main routine
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    // Get client connection info for printing
    Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE,
                client_port, MAXLINE, 0);
    printf("Opened connection with (%s, %s)\n", client_hostname, client_port);

    // Create listening thread that receives data
    pthread_mutex_lock(&nclients_lock);
    nclients++;
    pthread_mutex_unlock(&nclients_lock);
    Pthread_create(&tid[nclients], NULL, client_job, connfdp);
  }
  return 0;
}

/*******************************************************
 * Thread routine that listens to messages from a client
 * and reads packets into the ring buffer. Blocks when
 * buffer is full and exits when it receives notification
 * from the client that all packets have been sent.
 * ****************************************************/
void *client_job(void *varargp)
{
  int connfd = *((int *)varargp);
  int npackets, received = 0, cnt = 0, checksum = 1;
  long total_size = 0;
  time_t start_t, receive_t, clock_bias;
  char msg[MAXLINE];
  ssize_t nbytes;
  buf_item *cache_buf = (buf_item *)Malloc(sizeof(buf_item));
  float packet_bw, total_bw = 0.;
  struct timeval tv;

  start_t = get_time_ms(&tv);

  Pthread_detach(Pthread_self());
  Free(varargp);

  rio_t rio_client;
  Rio_readinitb(&rio_client, connfd);

  /* 1. Read the client's wall time and compute the bias */
  nbytes = Rio_readnb(&rio_client,msg,MAXLINE);
  clock_bias = get_time_ms(&tv) - atol(msg); 
  printf("Clock bias = %.3f s\n",clock_bias/1000.); 

  /* 2. Read how many packets to expect from the client */
  nbytes = Rio_readnb(&rio_client,msg,MAXLINE);
  npackets = atoi(msg);
    
  printf("Reading %d incoming packets...\n",npackets);

  /* 3. Read the packets from the client */
  while ((nbytes = Rio_readnb(&rio_client,msg,MAXLINE)) > 0) { 

    // Check if the client is ready to send or is finished
    if (!strcmp(msg,"CLIENT_FINISHED")) {
      break;
    } else if (strcmp(msg,"CLIENT_READY"))
      continue;

    sem_wait(&buf->spacesem); // wait until buffer opens up

    // Acknowledge client
    strncpy(msg,"ACK",MAXLINE);
    Rio_writen(connfd,msg,MAXLINE);

    // Read a packet from the client 
    nbytes = Rio_readnb(&rio_client,cache_buf,sizeof(buf_item));
    if (nbytes != sizeof(buf_item)) {
      fprintf(stderr,"  [%3d%%] -> Error: Packet has wrong size, closing connection with client\n",100*(cnt+1)/npackets);
      break;
    }

    // Compute packet transmission time in ms
    receive_t = get_time_ms(&tv) - cache_buf->timestamp - clock_bias;

    // Send the measured bandwidth back to the client
    packet_bw = (receive_t == 0) ? 0. : nbytes/receive_t/1000.; // MB/s
    sprintf(msg,"%f",packet_bw);
    Rio_writen(connfd,msg,MAXLINE);

    total_bw += packet_bw;
    total_size += nbytes;
    cnt += 1;

    if (use_checksum)
      checksum = md5checksum((char *)cache_buf,sizeof(buf_item));

    // Add received packet to ring buffer if checksum is correct
    if (checksum){
      enqueue(buf,cache_buf); // copy cache_buf into the main buffer
      received += 1;

      /* Print packet information */
      printf("  [%3d%%] -> received packet | %.2f MB | %6.1f MB/s\n",\
             100*cnt/npackets,\
             nbytes/MEGABYTE,\
             packet_bw);
      if (verbose)
        print_buffer(buf); // Print current buffer state
    } else {
      fprintf(stderr,"  [%3d%%] -> Error: invalid checksum in packet, skipping.\n",\
             100*cnt/npackets);
      sem_post(&buf->spacesem); // roll back the buf space semaphore
    }
  }

  total_bw = (cnt == 0) ? 0. : total_bw/cnt;

  printf("----------------------------------------------------------------\n");
  if(received != cnt)
    printf("WARNING: Some packets were not received!\n");
  printf("%d/%d packets received, closing connection with client\n",\
         received,npackets);
  printf("Total data received: %.2f MB\n",total_size/MEGABYTE);
  printf("Average bandwidth: %.1f MB/s\n",total_bw);
  printf("Total time: %.1f s\n",(get_time_ms(&tv) - start_t)/1000.);
  printf("----------------------------------------------------------------\n");

  if (received == 0)
    printf("No packets in processing queue, waiting...\n");

  /* Signal that the thread is about to end */
  pthread_mutex_lock(&nclients_lock);
  nclients--;
  pthread_mutex_unlock(&nclients_lock);
  Free(cache_buf);
  Close(connfd);
  return NULL;
}

/*******************************************************************
 * Thread routine that continuously reads items from the buffer 
 * and processes them. Handles different cases when buffer is empty.
 *******************************************************************/
void *buffer_job() 
{
  Pthread_detach(Pthread_self());
  int sval = buf->n_items, flag = 0;

  while (1) {

    /* What to do when buffer is empty */
    while (sval == buf->n_items) {
      if (!flag && nclients > 0) {
        flag = 1; 
      } else if (flag && nclients == 0) { 
        printf("No packets in processing queue, waiting...\n"); 
        flag = 0;
      }
      sem_getvalue(&buf->spacesem,&sval);
    }

    /* What to do when buffer has queued items */
    while (sval < buf->n_items) {
      dequeue(buf);
      sem_getvalue(&buf->spacesem,&sval);
    }
  }
  return NULL;
}

/************************************************
 * Close any open file descriptors
 * **********************************************/
void close_openfds(int *clientfd, int *serverfd) 
{
  if (*clientfd >= 0)
      Close(*clientfd);
  if (*serverfd >= 0)
      Close(*serverfd);
}

/********************************************
 * Free ring buffer memory upon exit (ctrl-c)
 * ******************************************/
void sigint_handler(int sig)
{
  destroy_buf(buf);
  exit(0);
}

void print_usage()
{
  fprintf(stderr, "Usage: ./server <port> [-options]\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -n <int> number of packets that can be held in buffer (default=8)\n");
  fprintf(stderr, "  -c       use MD5 checksumming on packets (warning: is slow)\n");
  fprintf(stderr, "  -h       usage\n");
  fprintf(stderr, "  -v       print buffer contents after enqueuing each packet\n");
}
