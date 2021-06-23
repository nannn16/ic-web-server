/* Name: Thuntita Kongiamtrakun
 * ID: 6180899
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "queue.h"

void queue_init(Queue *q, int size) {
    q->size = size;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

void push(Queue *q, int connFd) {
    q->data[q->tail] = connFd;
    q->tail = (q->tail + 1) % q->size;
    q->count = q->count + 1;
}

int pop(Queue *q) {
    if(isEmpty(q)) {
        return -1;
    }
    int p = q->data[q->head];
    q->head = (q->head + 1) % q->size;
    q->count = q->count - 1;
    return p;
}

int isEmpty(Queue *q) {
    if(q->count<=0) {
        return 1;
    }
    return 0;
    // IMPLEMENT
}

/***** Expected output: *****
No items
a
b
c
a
b
c
d
e
f
No items
s = World
t = Hello
*****************************/
