/* http_conn.c */
#include "http_conn.h"
#include "log.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static http_conn *g_conns = NULL;
static int g_max_conn = 0;
static int g_epollfd = -1;
static const char *DOC_ROOT = "./root";

/* ---------- 工具函数 ---------- */
static int set_nonblocking(int fd) {
        int flags = fcntl(fd, F_GETFL);
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ---------- 连接池管理 ---------- */
void http_init(int epollfd, int max_conn) {
        g_epollfd = epollfd;
        g_max_conn = max_conn;
        g_conns = calloc(max_conn, sizeof(http_conn));
        for (int i = 0; i < max_conn; i++) {
                g_conns[i].m_sockfd = -1;
                g_conns[i].m_epollfd = NULL;
        }
}

http_conn *http_get_free_conn(void) {
        for (int i = 0; i < g_max_conn; i++) {
                if (g_conns[i].m_sockfd == -1)
                        return &g_conns[i];
        }
        return NULL;
}

void http_release_conn(http_conn *conn) {
        if (conn)
                conn->m_sockfd = -1;
}

/* ---------- 连接初始化(接受新连接后调用) ---------- */
static void http_init_conn(http_conn *conn, int sockfd,
                           struct sockaddr_in *addr) {
        conn->m_sockfd = sockfd;
        conn->m_address = *addr;
        conn->m_read_idx = 0;
        conn->m_checked_idx = 0;
        conn->m_write_idx = 0;
        conn->m_check_state = CHECK_STATE_REQUESTLINE;
        conn->m_content_length = 0;
        conn->m_linger = 0;
        conn->m_url[0] = '\0';
        conn->m_method[0] = '\0';
}

/* ---------- 读取数据 ---------- */
int http_read_once(http_conn *conn) {
        if (conn->m_read_idx >= READ_BUFFER_SIZE)
                return 0;

        int bytes_read = 0;
        while (1) {
                bytes_read =
                    recv(conn->m_sockfd, conn->m_read_buf + conn->m_read_idx,
                         READ_BUFFER_SIZE - conn->m_read_idx, 0);
                if (bytes_read == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                        return 0;
                } else if (bytes_read == 0) {
                        return 0; /* 对方关闭连接 */
                }
                conn->m_read_idx += bytes_read;
        }
        return 1;
}

/* ---------- 解析一行 ---------- */
static HTTP_CODE parse_line(http_conn *conn, char *text) {
        (void)conn;
        if (!text)
                return BAD_REQUEST;

        /* 处理请求行 */
        if (conn->m_check_state == CHECK_STATE_REQUESTLINE) {
                char *method = text;
                char *url = strchr(method, ' ');
                if (!url)
                        return BAD_REQUEST;
                *url++ = '\0';

                char *version = strchr(url, ' ');
                if (!version)
                        return BAD_REQUEST;
                *version++ = '\0';
                while (*version == ' ')
                        version++;

                if (strcasecmp(method, "GET") == 0) {
                        strncpy(conn->m_url, url, FILENAME_LEN - 1);
                        conn->m_check_state = CHECK_STATE_HEADER;
                        return NO_REQUEST;
                }
                return BAD_REQUEST;
        }

        /* 处理头部 */
        if (conn->m_check_state == CHECK_STATE_HEADER) {
                if (text[0] == '\0') {
                        /* 空行表示头部结束 */
                        return GET_REQUEST;
                }
                if (strncasecmp(text, "Connection:", 11) == 0) {
                        text += 11;
                        while (*text == ' ')
                                text++;
                        if (strcasecmp(text, "keep-alive") == 0)
                                conn->m_linger = 1;
                } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
                        text += 15;
                        while (*text == ' ')
                                text++;
                        conn->m_content_length = atoi(text);
                } else if (strncasecmp(text, "Host:", 5) == 0) {
                        text += 5;
                        while (*text == ' ')
                                text++;
                        strncpy(conn->m_host, text, sizeof(conn->m_host) - 1);
                }
                return NO_REQUEST;
        }

        return BAD_REQUEST;
}

/* ---------- 主状态机 ---------- */
static HTTP_CODE process_read(http_conn *conn) {
        char *buf = conn->m_read_buf;
        int idx = conn->m_checked_idx;

        while (idx < conn->m_read_idx) {
                char *line_end = strstr(buf + idx, "\r\n");
                if (!line_end)
                        break; /* 行不完整，等待更多数据 */

                *line_end = '\0';
                char *line = buf + idx;
                idx = (line_end - buf) + 2;
                conn->m_checked_idx = idx;

                HTTP_CODE ret = parse_line(conn, line);
                if (ret == GET_REQUEST) {
                        conn->m_checked_idx = idx;
                        return GET_REQUEST;
                } else if (ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                }
        }

        /* 处理请求体(简化版，仅支持GET) */
        if (conn->m_check_state == CHECK_STATE_HEADER &&
            idx >= conn->m_read_idx) {
                return NO_REQUEST;
        }
        return NO_REQUEST;
}

/* ---------- 生成HTTP响应 ---------- */
static int add_response(http_conn *conn, const char *format, ...) {
        va_list args;
        va_start(args, format);
        int len =
            vsnprintf(conn->m_write_buf + conn->m_write_idx,
                      WRITE_BUFFER_SIZE - conn->m_write_idx, format, args);
        va_end(args);
        if (len > 0)
                conn->m_write_idx += len;
        return len;
}

static int process_write(http_conn *conn, HTTP_CODE ret) {
        conn->m_write_idx = 0;

        switch (ret) {
        case FILE_REQUEST: {
                /* 打开静态文件 */
                char path[256];
                if (strcmp(conn->m_url, "/") == 0)
                        snprintf(path, sizeof(path), "%s/index.html", DOC_ROOT);
                else
                        snprintf(path, sizeof(path), "%s%s", DOC_ROOT,
                                 conn->m_url);

                struct stat st;
                if (stat(path, &st) < 0 || !S_ISREG(st.st_mode))
                        return process_write(conn, BAD_REQUEST);

                int fd = open(path, O_RDONLY);
                if (fd < 0)
                        return process_write(conn, INTERNAL_ERROR);

                add_response(conn, "HTTP/1.1 200 OK\r\n");
                add_response(conn, "Content-Length: %ld\r\n", st.st_size);
                add_response(conn, "Content-Type: text/html\r\n");
                if (conn->m_linger)
                        add_response(conn, "Connection: keep-alive\r\n");
                else
                        add_response(conn, "Connection: close\r\n");
                add_response(conn, "\r\n");

                /* 直接发送文件内容(简化处理，实际应使用sendfile) */
                char *file_buf = malloc(st.st_size);
                read(fd, file_buf, st.st_size);
                close(fd);

                /* 合并头部和文件内容 */
                char *full = malloc(conn->m_write_idx + st.st_size);
                memcpy(full, conn->m_write_buf, conn->m_write_idx);
                memcpy(full + conn->m_write_idx, file_buf, st.st_size);
                int total = conn->m_write_idx + st.st_size;
                free(file_buf);

                int sent = send(conn->m_sockfd, full, total, 0);
                free(full);
                return sent;
        }
        case BAD_REQUEST: {
                const char *body =
                    "<html><body><h1>400 Bad Request</h1></body></html>";
                add_response(conn, "HTTP/1.1 400 Bad Request\r\n");
                add_response(conn, "Content-Length: %zu\r\n", strlen(body));
                add_response(conn, "Content-Type: text/html\r\n\r\n");
                add_response(conn, "%s", body);
                return send(conn->m_sockfd, conn->m_write_buf,
                            conn->m_write_idx, 0);
        }
        case INTERNAL_ERROR: {
                const char *body =
                    "<html><body><h1>500 Internal Error</h1></body></html>";
                add_response(conn, "HTTP/1.1 500 Internal Error\r\n");
                add_response(conn, "Content-Length: %zu\r\n", strlen(body));
                add_response(conn, "Content-Type: text/html\r\n\r\n");
                add_response(conn, "%s", body);
                return send(conn->m_sockfd, conn->m_write_buf,
                            conn->m_write_idx, 0);
        }
        default:
                break;
        }
        return -1;
}

/* ---------- 处理HTTP请求 ---------- */
void http_process(http_conn *conn) {
        HTTP_CODE ret = process_read(conn);
        if (ret == NO_REQUEST) {
                /* 数据不完整，重新注册EPOLLIN */
                struct epoll_event ev;
                ev.data.fd = conn->m_sockfd;
                ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                epoll_ctl(g_epollfd, EPOLL_CTL_MOD, conn->m_sockfd, &ev);
                return;
        }

        process_write(conn, ret);

        if (!conn->m_linger) {
                http_close_conn(conn);
        } else {
                /* 重置状态，保持连接 */
                conn->m_read_idx = 0;
                conn->m_checked_idx = 0;
                conn->m_check_state = CHECK_STATE_REQUESTLINE;
                struct epoll_event ev;
                ev.data.fd = conn->m_sockfd;
                ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                epoll_ctl(g_epollfd, EPOLL_CTL_MOD, conn->m_sockfd, &ev);
        }
}

/* ---------- 关闭连接 ---------- */
void http_close_conn(http_conn *conn) {
        if (conn->m_sockfd == -1)
                return;
        epoll_ctl(g_epollfd, EPOLL_CTL_DEL, conn->m_sockfd, NULL);
        close(conn->m_sockfd);
        conn->m_sockfd = -1;
}