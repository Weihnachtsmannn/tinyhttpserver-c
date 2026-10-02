/**
 * @brief 一个简单的http服务器
 *
 * @note linux socket
 * @note linux epoll
 * @note http protocol
 */
#include "webserver.h"
#include <stdlib.h>

int main(int argc, char *argv[]) {
        int port = 9006;    /* 默认端口 */
        int thread_num = 8; /* 默认线程数 */

        if (argc > 1)
                port = atoi(argv[1]);
        if (argc > 2)
                thread_num = atoi(argv[2]);

        webserver_run(port, thread_num);
        return 0;
}