/* threadpool.h */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

typedef struct task {
    void (*func)(void *);
    void *arg;
} task_t;

typedef struct threadpool {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    pthread_t      *threads;
    task_t         *queue;
    int             queue_size;
    int             queue_capacity;
    int             queue_front;
    int             queue_rear;
    int             thread_count;
    int             shutdown;
} threadpool_t;

threadpool_t *threadpool_create(int thread_count, int queue_capacity);
int  threadpool_add(threadpool_t *pool, void (*func)(void *), void *arg);
void threadpool_destroy(threadpool_t *pool);

#endif