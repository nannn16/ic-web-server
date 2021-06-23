#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "queue.h"
#include "threadpool.h"

void threadpool_add(struct threadpool_t *pool, int connFd) {
    pthread_mutex_lock(&(pool->jobs_mutex));
    push(&(pool->jobs), connFd);
    pthread_cond_signal(&(pool->jobs_cond));
    pthread_mutex_unlock(&(pool->jobs_mutex));
}

void threadpool_create(struct threadpool_t *threadpool, int numThreads) {
    queue_init(&(threadpool->jobs), MAXSIZE);
    pthread_t threads[numThreads];
    pthread_mutex_init(&(threadpool->jobs_mutex), NULL);
    pthread_cond_init(&(threadpool->jobs_cond), NULL);

    for (int i=0; i<numThreads; i++) {
        pthread_create(&(threads[i]), NULL, do_work, (void*)threadpool);
    }
}