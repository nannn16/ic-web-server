struct threadpool_t {
    Queue jobs;
    pthread_mutex_t jobs_mutex;
    pthread_cond_t jobs_cond;
};

void* do_work(void *arg);
void threadpool_add(struct threadpool_t *pool, int connFd);
void threadpool_create(struct threadpool_t *threadpool, int numThreads);