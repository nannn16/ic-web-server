#ifndef __SIMPLE_WORK_QUEUE_HPP_
#define __SIMPLE_WORK_QUEUE_HPP_

#include<deque>
#include<pthread.h>

using namespace std;

struct survival_bag {
    int connFd;
    char rootFolder[1024];
};

struct threadpool_t {
    deque<survival_bag> jobs;
    pthread_mutex_t jobs_mutex;
    pthread_cond_t jobs_cond;
    pthread_t *threads;

    /* add a new job to the work queue
     * and return the number of jobs in the queue */
    int add_job(survival_bag context) {
        pthread_mutex_lock(&this->jobs_mutex);
        jobs.push_back(context);
        pthread_cond_signal(&this->jobs_cond);
        size_t len = jobs.size();
        pthread_mutex_unlock(&this->jobs_mutex);
        return len;
    }
    
    /* return FALSE if no job is returned
     * otherwise return TRUE and set *job to the job */
    bool remove_job(survival_bag *job) {
        pthread_mutex_lock(&this->jobs_mutex);
        while(this->jobs.empty()) {
            pthread_cond_wait(&this->jobs_cond, &this->jobs_mutex);
        }
        bool success = !this->jobs.empty();
        if (success) {
            *job = this->jobs.front();
            this->jobs.pop_front();
        }
        pthread_mutex_unlock(&this->jobs_mutex);
        return success;
    }
};

#endif
