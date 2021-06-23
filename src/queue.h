/* Do not modify */
#ifndef CQUEUE_H
#define CQUEUE_H

#define MAXSIZE 8192

struct queue_struct {
    int data[8192];
    int size;
    int head;
    int tail;
    int count;
};

typedef struct queue_struct Queue;

void queue_init(Queue *q, int size);

/* Push a word to the back of this queue
 * You must keep a *COPY* of the word.
 * If q is NULL, allocate space for it here
 */
void push(Queue *q, int connFd);

/* Returns the data at the front of the queue
 * and remove it from the queue as well.
 * If q is empty, return NULL
 */
int pop(Queue *q);

/* Checks if the queue is empty */
int isEmpty(Queue *q);

#endif
