/************************************************************************** 
 * Client routine simulates connecting to an image processing server on
 * a network (such as a Tegra TX1) and sending over a number of large image
 * data packets (i.e. SAR or Optical). The server has a limited amount of
 * space so it queues the packets in a ring buffer while processing. If the
 * buffer is full and there is no space available, the server read thread
 * blocks and the client is unable to send anything until a spot opens up. 
 *
 * Author: Aleksander Bapst
 **************************************************************************/

#include "ring_buffer.h"

/* Function Declarations */
void print_usage();

int use_checksum = 0;

int main(int argc, char **argv)
{
  int clientfd, ii, opt, err_flag = 0, npackets = DEFAULT_NPACKETS;
  float total_size, packet_bw;
  float avg_bw, total_bw = 0;
  size_t packet_size = sizeof(buf_item);
  time_t start_t;
  char *host_ip, *port, msg[MAXLINE];
  buf_item *packet = (buf_item *)Malloc(sizeof(buf_item));
  rio_t rio_server;
  struct timeval tv;

  struct sockaddr_storage servaddr;
  socklen_t servlen = sizeof(struct sockaddr_storage);
  char host_name[MAXLINE], host_service[MAXLINE];

  start_t = get_time_ms(&tv);

  if (argc < 3) {
    print_usage();
    exit(0);
  }

  /* Required args */
  host_ip = argv[1];
  port = argv[2];

  /* Parse optional args */
  while ((opt = getopt(argc, argv, "n:ch")) != -1) {
    switch(opt) {
      case 'n':
        npackets = atoi(optarg);
        break;
      case 'c':
        use_checksum = 1;
        break;
      case 'h':
        print_usage();
        exit(0);
      default:
        continue;
    } 
  }

  /* Open connection to server */
  clientfd = Open_clientfd(host_ip, port, (SA *)&servaddr);
  Rio_readinitb(&rio_server, clientfd);

  Getnameinfo((SA *)&servaddr, servlen,
              host_name, MAXLINE,
              host_service, MAXLINE, 0);

  printf("----------------------------------------------------------------\n");
  printf("Opened connection with %s at (%s, %s)\n",\
         host_name,host_ip,host_service);
  if (use_checksum)
    printf("[Using MD5 checksum]\n");
  printf("----------------------------------------------------------------\n");
  printf("Sending %d packets...\n",npackets);

  /* 1. Send clock time in ms to server so it can compute clock bias */
  sprintf(msg,"%ld",get_time_ms(&tv));
  Rio_writen(clientfd,msg,MAXLINE);

  /* 2. Tell server how many packets to expect */
  sprintf(msg,"%d",npackets);
  Rio_writen(clientfd,msg,MAXLINE);

  /* 3. Send packets to the destination */
  for (ii = 0; ii < npackets; ii++) {

    packet->id = ii; // assign unique id to packet

    if (use_checksum)
      md5checksum((char *)packet,sizeof(buf_item)); // set checksum

    /* Tell server a packet is coming */
    strncpy(msg,"CLIENT_READY",MAXLINE);
    Rio_writen(clientfd,msg,MAXLINE);

    /* Listen for acknowledgement from server */
    Rio_readnb(&rio_server,msg,MAXLINE); 
    if (!strcmp(msg,"ACK")) {

      packet->timestamp = get_time_ms(&tv);

      /* Send a packet to the server */
      Rio_writen(clientfd, packet, sizeof(buf_item));

      /* Receive transmission bandwidth from server */
      Rio_readnb(&rio_server,msg,MAXLINE);
      packet_bw = atof(msg);
      total_bw += packet_bw;

      printf("  [%3d%%] -> sent packet | %.2f MB | %6.1f MB/s\n",\
             100*(ii+1)/npackets,\
             packet_size/MEGABYTE,\
             packet_bw);
      } else {
        err_flag = 1;
        break;
      }
  }

  /* Tell the server there are no more packets to send */
  strncpy(msg,"CLIENT_FINISHED",MAXLINE);
  Rio_writen(clientfd,msg,MAXLINE);

  /* Compute statistics */
  total_size = ii*packet_size/MEGABYTE;
  avg_bw = (ii == 0) ? 0. : total_bw/ii;

  printf("----------------------------------------------------------------\n");
  if (err_flag)
    fprintf(stderr,\
            "Unknown signal from host, terminating with %d/%d packets sent.\n",\
            ii,npackets);
  else
    printf("%d/%d packets sent, closing connection with host.\n",ii,npackets);
  printf("Total data sent: %.2f MB\n",total_size);
  printf("Average bandwidth: %.1f MB/s\n",avg_bw);
  printf("Total time: %.1f s\n",(get_time_ms(&tv) - start_t)/1000.);
  printf("----------------------------------------------------------------\n");

  Free(packet);
  Close(clientfd);
  exit(0);
}

void print_usage()
{
  fprintf(stderr, "Usage: ./client <host_ip> <port> [-options]\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -n <int> number of packets to send (default=16)\n");
  fprintf(stderr, "  -c       use MD5 checksumming of packets (warning: is slow)\n");
  fprintf(stderr, "  -h       print usage\n");
}
