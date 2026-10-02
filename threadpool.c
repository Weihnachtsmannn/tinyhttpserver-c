/* threadpool.c */
#include "threadpool.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

static void *worker(void *arg) {
    threadpool_t *pool = (threadpool_t *)arg;
    while (1) {
        pthread_mutex_lock(&pool->lock);
        while (pool->queue_size == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->cond, &pool->lock);

        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        task_t task = pool->queue[pool->queue_front];
        pool->queue_front = (pool->queue_front + 1) % pool->queue_capacity;
        pool->queue_size--;

        pthread_mutex_unlock(&pool->lock);

        task.func(task.arg);   /* 执行任务 */
    }
    return NULL;
}

threadpool_t *threadpool_create(int thread_count, int queue_capacity) {
    threadpool_t *pool = calloc(1, sizeof(threadpool_t));
    pool->thread_count     = thread_count;
    pool->queue_capacity   = queue_capacity;
    pool->threads          = calloc(thread_count, sizeof(pthread_t));
    pool->queue            = calloc(queue_capacity, sizeof(task_t));
    pool->queue_front      = 0;
    pool->queue_rear       = 0;
    pool->queue_size       = 0;
    pool->shutdown         = 0;

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (int i = 0; i < thread_count; i++)
        pthread_create(&pool->threads[i], NULL, worker, pool);

    return pool;
}

int threadpool_add(threadpool_t *pool, void (*func)(void *), void *arg) {
    pthread_mutex_lock(&pool->lock);

    if (pool->queue_size == pool->queue_capacity) {
        pthread_mutex_unlock(&pool->lock);
        return -1;   /* 队列满 */
    }

    pool->queue[pool->queue_rear].func = func;
    pool->queue[pool->queue_rear].arg  = arg;
    pool->queue_rear = (pool->queue_rear + 1) % pool->queue_capacity;
    pool->queue_size++;

    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

void threadpool_destroy(threadpool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->thread_count; i++)
        pthread_join(pool->threads[i], NULL);

    free(pool->threads);
    free(pool->queue);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    free(pool);
}