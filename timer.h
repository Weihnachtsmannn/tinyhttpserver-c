/* timer.h */
#ifndef TIMER_H
#define TIMER_H

#include <time.h>

#define TIMESLOT 5 /* 定时器检查间隔(秒) */

typedef struct client_data {
        int sockfd;
        struct sockaddr_in *address;
        struct timer_node *timer;
} client_data;

typedef struct timer_node {
        time_t expire; /* 绝对超时时间 */
        void (*callback)(client_data *);
        client_data *user_data;
} timer_node;

/* 初始化小根堆 */
void timer_heap_init(int capacity);
/* 添加定时器，返回节点指针 */
timer_node *timer_add(int sockfd, time_t expire, void (*cb)(client_data *),
                      client_data *ud);
/* 调整定时器超时时间(延迟) */
void timer_adjust(timer_node *timer, time_t new_expire);
/* 删除定时器 */
void timer_del(timer_node *timer);
/* 处理超时事件(在SIGALRM信号处理中被调用) */
void timer_tick(void);
/* 释放资源 */
void timer_destroy(void);

#endif