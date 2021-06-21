/* Name: Thuntita Kongiamtrakun
 * ID: 6180899
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "queue.h"

void push(Queue **q, int connFd) {
    if(*q == NULL) {
        *q = (Queue*) malloc(sizeof(Queue));
        (*q)->head = NULL;
        (*q)->tail = NULL;
    }

    Node *newNode = (Node*) malloc(sizeof(Node));
    newNode->data = connFd;
    newNode->next = NULL;

    if((*q)->head == NULL) {
        (*q)->head = newNode;
        (*q)->tail = newNode;
    }

    else {
        (*q)->tail->next = newNode;
        (*q)->tail = (*q)->tail->next;
    }
    // IMPLEMENT
}

int pop(Queue *q) {
    if(isEmpty(q)) {
        return -1;
    }
    int p = q->head->data;
    Node *temp = q->head;
    q->head = q->head->next;
    if(q->head==NULL) {
        q->tail = NULL;
    }
    free(temp);
    return p;
    // IMPLEMENT
}

void print(Queue *q) {
    if(isEmpty(q)) {
        printf("No items\n");
    }
    else {
        Node *cur = q->head;
        while(cur!=NULL) {
            printf("%d\n", cur->data);
            cur = cur->next;
        }
    }
    // IMPLEMENT
}

int isEmpty(Queue *q) {
    if(q==NULL) {
        return 1;
    }
    if(q->head == NULL) {
        return 1;
    }
    return 0;
    // IMPLEMENT
}

void deleteQueue(Queue *q) {
    while(q->head!=NULL) {
        Node *cur = q->head;
        int p = cur->data;
        q->head = q->head->next;
        free(cur);
    }
    q->tail = NULL;
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
