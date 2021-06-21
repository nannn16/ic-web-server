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

struct threadpool_t *threadpool_create(int numThreads) {
    struct threadpool_t *threadpool = (struct threadpool_t*) malloc(sizeof(struct threadpool_t));

    threadpool->threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);

    pthread_mutex_init(&(threadpool->jobs_mutex), NULL);
    pthread_cond_init(&(threadpool->jobs_cond), NULL);


    for (int i=0; i<numThreads; i++) {
        pthread_create(&(threadpool->threads[i]), NULL, do_work, (void*)threadpool);
    }
    threadpool->jobs = NULL;

    return threadpool;
}

int threadpool_free(struct threadpool_t *pool) {
    if (pool == NULL) {
        return 0;
    }

    if(pool->threads) {
        free(pool->threads);
        pthread_mutex_lock(&(pool->jobs_mutex));
        pthread_mutex_destroy(&(pool->jobs_mutex));
        pthread_cond_destroy(&(pool->jobs_cond));
    }
    free(pool);
    return 1;
}