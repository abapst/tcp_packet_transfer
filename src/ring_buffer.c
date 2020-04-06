/******************************************
 * Simple ring buffer implementation.
 *
 * Author: Aleksander Bapst
 * ****************************************/
#include "ring_buffer.h"

/******************************************************
 * Allocate ring buffer memory and initialize fields
 * ****************************************************/
ring_buffer *init_buf(int n_items)
{
  int ii;

  // allocate ring buffer struct
  ring_buffer *buf = (ring_buffer *)Malloc(sizeof(ring_buffer) + n_items*sizeof(buf_item*));
  buf->n_items = n_items;

  // initialize semaphores and locks
  pthread_mutex_init(&buf->lock,NULL);
  Sem_init(&buf->countsem,0,0);
  Sem_init(&buf->spacesem,0,buf->n_items);

  // initialize read and write index trackers
  buf->read = 0;
  buf->write = 0;

  // Allocate buffer items
  for (ii = 0; ii < buf->n_items; ii++) {
    buf->data[ii] = (buf_item *)Malloc(sizeof(buf_item));
    buf->data[ii]->id = -1; // items initialized with id -1 (empty)
  }

  return buf;
}

/*****************************************************
 * Free all the memory allocated to the ring buffer
 * ***************************************************/
void destroy_buf(ring_buffer *buf)
{
  int ii;

  printf("\nSIGINT caught, deleting ring buffer\n");
  for (ii = 0; ii < buf->n_items; ii++) {
    Free(buf->data[ii]);
  }
  Free(buf);
}

void enqueue(ring_buffer *buf, buf_item *cache_buf)
{
  pthread_mutex_lock(&buf->lock);
  buf_item *item = buf->data[ (buf->write++) & ((buf->n_items)-1) ];
  memcpy(item,cache_buf,sizeof(buf_item));
  pthread_mutex_unlock(&buf->lock);

  // increment the count of the number of items
  sem_post(&buf->countsem);
}

/******************************************************
 * Return a pointer to the next ring buffer item
 * ****************************************************/
void dequeue(ring_buffer *buf)
{
  // wait if there are no items in the buffer
  sem_wait(&buf->countsem);

  pthread_mutex_lock(&buf->lock);
  buf_item *item = buf->data[ (buf->read++) & ((buf->n_items)-1) ];
  pthread_mutex_unlock(&buf->lock);

  process_item(item);
  item->id = -1; // mark item as processed

  // increment number of spaces in the buffer
  sem_post(&buf->spacesem);
}

/*********************************************************************
 * Simulate processing of a buffer item
 * *******************************************************************/
void process_item(buf_item *item)
{
  usleep(0);
}

/*********************************************************************
 * Pretty-print the contents of the server ring buffer.
 * *******************************************************************/
void print_buffer(ring_buffer *buf)
{
  int ii;

  printf("     ");
  for (ii = 0; ii < buf->n_items; ii++) {
    if (buf->data[ii]->id == -1)
      printf("| -- ");
    else
      printf("| %02d ",buf->data[ii]->id);
  }
  printf("|\n");
}

/*********************************************************************
 * Returns the time in ms since the epoch as a long.
 * *******************************************************************/
time_t get_time_ms(struct timeval *tv)
{
  gettimeofday(tv,NULL);

  return tv->tv_sec*1000 + (time_t)tv->tv_usec/1000;
}

/*********************************************************************
 * Computes the MD5 checksum of a buffer item (with the checksum  and
 * timestamp fields set to 0), and updates the checksum field to the
 * checksum value. The original checksum is compared to the new one
 * and returns 1 if they match, else 0.
 *
 * Note: the MD5 hash is secure, but too slow for data transmission.
 *********************************************************************/
int md5checksum(char *item,size_t length)
{
  int ii;
  unsigned char new_checksum[MD5_DIGEST_LENGTH];
  unsigned char old_checksum[MD5_DIGEST_LENGTH];
  char *read_ptr = item;
  MD5_CTX c;

  /* Save old checksum */
  memcpy(old_checksum,((buf_item *)item)->checksum,MD5_DIGEST_LENGTH);

  memset(((buf_item *)item)->checksum,0,MD5_DIGEST_LENGTH);
  ((buf_item *)item)->timestamp = 0;

  /* Digest the packet */
  MD5_Init(&c);
  while (length > 0) {
   if (length > MAXLINE) {
      MD5_Update(&c,read_ptr,MAXLINE);
    } else {
      MD5_Update(&c,read_ptr,length);
      break;
    }
    length -= MAXLINE;
    read_ptr += MAXLINE;
  }
  MD5_Final(new_checksum,&c);

  /* Set new checksum */
  memcpy(((buf_item *)item)->checksum,new_checksum,MD5_DIGEST_LENGTH);

  /* Compare old and new checksums */
  for (ii = 0; ii < MD5_DIGEST_LENGTH; ii++) {
    if (old_checksum[ii] != new_checksum[ii])
      return 0;
  }
  return 1;
}

/***************************************************************
 * Print the MD5 checksum field of a buffer item in hexadecimal
 * *************************************************************/
void print_checksum(buf_item *item)
{
  int ii;

  printf("MD5 = ");
  for (ii = 0; ii < MD5_DIGEST_LENGTH; ii++) {
    printf("%02x",(unsigned char)item->checksum[ii]);
  }
  printf("\n");
}
