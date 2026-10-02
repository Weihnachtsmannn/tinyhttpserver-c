/* webserver.c */
#include "webserver.h"
#include "http_conn.h"
#include "log.h"
#include "threadpool.h"
#include "timer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_epollfd = -1;
static int g_listenfd = -1;
static int g_pipefd[2]; /* 统一事件源：信号管道 */
static threadpool_t *g_pool = NULL;

static int set_nonblocking(int fd) {
        int flags = fcntl(fd, F_GETFL);
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ---------- 信号处理 ---------- */
static void sig_handler(int sig) {
        int save_errno = errno;
        int msg = sig;
        send(g_pipefd[1], (char *)&msg, 1, 0);
        errno = save_errno;
}

static void addsig(int sig) {
        struct sigaction sa;
        memset(&sa, '\0', sizeof(sa));
        sa.sa_handler = sig_handler;
        sa.sa_flags |= SA_RESTART;
        sigfillset(&sa.sa_mask);
        sigaction(sig, &sa, NULL);
}

/* ---------- 设置非阻塞并注册到epoll ---------- */
static void addfd(int epollfd, int fd, int oneshot) {
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        if (oneshot)
                ev.events |= EPOLLONESHOT;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
        set_nonblocking(fd);
}

/* ---------- 定时器回调：关闭超时连接 ---------- */
static void cb_close_conn(client_data *user_data) {
        http_close_conn((http_conn *)user_data
                            ->address); /* 简化：address 存 http_conn 指针 */
        timer_del(user_data->timer);
        free(user_data->address);
        free(user_data);
}

/* ---------- 处理新连接 ---------- */
static void handle_new_connection(void) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (1) {
                int connfd = accept(g_listenfd, (struct sockaddr *)&client_addr,
                                    &client_len);
                if (connfd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                        continue;
                }

                http_conn *conn = http_get_free_conn();
                if (!conn) {
                        close(connfd);
                        continue;
                }

                /* 初始化连接 */
                extern void http_init_conn_public(http_conn * conn, int sockfd,
                                                  struct sockaddr_in *addr);
                http_init_conn_public(conn, connfd, &client_addr);

                /* 注册到epoll */
                addfd(g_epollfd, connfd, 1);

                /* 添加定时器 */
                client_data *ud = malloc(sizeof(client_data));
                ud->sockfd = connfd;
                ud->address = (struct sockaddr_in *)conn; /* 存 conn 指针 */
                ud->timer = timer_add(connfd, time(NULL) + 3 * TIMESLOT,
                                      cb_close_conn, ud);

                conn->m_epollfd =
                    ud->timer; /* 反向引用，用于调整定时器(简化) */
        }
}

/* ---------- 处理读事件 ---------- */
static void handle_read(http_conn *conn) {
        if (!http_read_once(conn)) {
                http_close_conn(conn);
                return;
        }
        /* 调整定时器 */
        /* timer_adjust(...);  实际需保存 timer_node 指针 */

        /* 提交任务到线程池 */
        extern void http_process_public(http_conn * conn);
        threadpool_add(g_pool, (void (*)(void *))http_process_public, conn);
}

/* ---------- 处理信号 ---------- */
static void handle_signal(void) {
        char signals[1024];
        int ret = recv(g_pipefd[0], signals, sizeof(signals), 0);
        if (ret <= 0)
                return;

        for (int i = 0; i < ret; i++) {
                switch (signals[i]) {
                case SIGALRM:
                        timer_tick();
                        break;
                case SIGTERM:
                        log_close();
                        exit(0);
                default:
                        break;
                }
        }
}

/* ---------- 主事件循环 ---------- */
void webserver_run(int port, int thread_num) {
        /* 初始化日志 */
        log_init("server.log", 5000000);

        /* 初始化线程池 */
        g_pool = threadpool_create(thread_num, 10000);

        /* 创建监听socket */
        g_listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (g_listenfd == -1) {
                LOG_ERROR("socket create failed: %s", strerror(errno));
                exit(1);
        }

        int opt = 1;
        setsockopt(g_listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(g_listenfd, (struct sockaddr *)&address, sizeof(address)) ==
            -1) {
                LOG_ERROR("bind failed: %s", strerror(errno));
                exit(1);
        }

        if (listen(g_listenfd, 128) == -1) {
                LOG_ERROR("listen failed: %s", strerror(errno));
                exit(1);
        }

        /* 创建epoll实例 */
        g_epollfd = epoll_create1(0);

        /* 初始化连接池 */
        http_init(g_epollfd, MAX_FD);

        /* 注册监听socket */
        addfd(g_epollfd, g_listenfd, 0);

        /* 统一事件源：信号处理 */
        socketpair(AF_UNIX, SOCK_STREAM, 0, g_pipefd);
        set_nonblocking(g_pipefd[1]);

        addsig(SIGTERM);
        addsig(SIGALRM);

        /* 设置定时器 */
        struct itimerval timeout;
        timeout.it_value.tv_sec = TIMESLOT;
        timeout.it_value.tv_usec = 0;
        timeout.it_interval.tv_sec = TIMESLOT;
        timeout.it_interval.tv_usec = 0;
        setitimer(ITIMER_REAL, &timeout, NULL);

        /* 将信号管道注册到epoll */
        addfd(g_epollfd, g_pipefd[0], 0);

        /* 初始化定时器堆 */
        timer_heap_init(MAX_FD);

        LOG_INFO("Server started on port %d with %d threads", port, thread_num);

        /* 主事件循环 */
        struct epoll_event events[MAX_EVENT_NUMBER];
        while (1) {
                int n = epoll_wait(g_epollfd, events, MAX_EVENT_NUMBER, -1);
                if (n < 0 && errno != EINTR) {
                        LOG_ERROR("epoll_wait error: %s", strerror(errno));
                        break;
                }

                for (int i = 0; i < n; i++) {
                        int fd = events[i].data.fd;

                        if (fd == g_listenfd) {
                                handle_new_connection();
                        } else if (fd == g_pipefd[0] &&
                                   (events[i].events & EPOLLIN)) {
                                handle_signal();
                        } else if (events[i].events &
                                   (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                                /* 连接异常，关闭 */
                                http_conn *conn = NULL;
                                /* 简化：遍历连接池查找 */
                                extern http_conn *http_find_conn_by_fd(int fd);
                                conn = http_find_conn_by_fd(fd);
                                if (conn)
                                        http_close_conn(conn);
                        } else if (events[i].events & EPOLLIN) {
                                extern http_conn *http_find_conn_by_fd(int fd);
                                http_conn *conn = http_find_conn_by_fd(fd);
                                if (conn)
                                        handle_read(conn);
                        }
                }
        }
}