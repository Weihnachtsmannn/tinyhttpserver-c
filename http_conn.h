/* http_conn.h */
#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <netinet/in.h>
#include <sys/epoll.h>

#define READ_BUFFER_SIZE 2048
#define WRITE_BUFFER_SIZE 1024
#define FILENAME_LEN 200

typedef enum {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
} CHECK_STATE;

typedef enum {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
} HTTP_CODE;

typedef struct http_conn {
        int m_sockfd;
        struct sockaddr_in m_address;

        char m_read_buf[READ_BUFFER_SIZE];
        int m_read_idx;
        int m_checked_idx;

        char m_write_buf[WRITE_BUFFER_SIZE];
        int m_write_idx;

        CHECK_STATE m_check_state;
        int m_content_length;
        int m_linger;

        char m_url[FILENAME_LEN];
        char m_method[16];
        char m_version[16];
        char m_host[100];

        struct epoll_event *m_epollfd;
} http_conn;

/* 连接池 */
void http_init(int epollfd, int max_conn);
http_conn *http_get_free_conn(void);
void http_release_conn(http_conn *conn);

/* 连接处理 */
int http_read_once(http_conn *conn);
int http_write(http_conn *conn);
void http_process(http_conn *conn);
void http_close_conn(http_conn *conn);

#endif