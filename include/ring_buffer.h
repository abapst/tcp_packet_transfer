/*****************************************************************************
 * Ring Buffer headers and declarations.
 *
 * Author: Aleksander Bapst
 * **************************************************************************/
#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include "safe_wrappers.h"
#include <openssl/md5.h>

#define MEGABYTE 1048576.
#define DEFAULT_BUFFER_SIZE 8
#define MAX_CLIENTS 5
#define DEFAULT_NPACKETS 16

typedef struct {
  float img_data[2][4096][4096];
  time_t timestamp; // time since epoch in ms
  unsigned char checksum[MD5_DIGEST_LENGTH];
  int id;
} buf_item;

typedef struct {
  int read; // index of read pointer
  int write; // index of write pointer
  int n_items;
  sem_t countsem, spacesem;
  pthread_mutex_t lock;
  buf_item *data[]; // flexible array member, will be malloc'd during init
} ring_buffer;

/* Ring buffer functions */
ring_buffer *init_buf(int n_items);
void destroy_buf(ring_buffer *buf);
void enqueue(ring_buffer *buf, buf_item *cache_buf);
void dequeue(ring_buffer *buf);
void print_buffer(ring_buffer *buf);
void process_item(buf_item *item);

/* Helper functions */
time_t get_time_ms(struct timeval *tv);
int md5checksum(char *item,size_t length);
void print_checksum(buf_item *item);

#endif
