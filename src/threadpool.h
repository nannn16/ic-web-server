struct threadpool_t {
    Queue jobs;
    pthread_mutex_t jobs_mutex;
    pthread_cond_t jobs_cond;
    pthread_t *threads;
};

void* do_work(void *arg);
void threadpool_add(struct threadpool_t *pool, int connFd);
struct threadpool_t *threadpool_create(int numThreads);