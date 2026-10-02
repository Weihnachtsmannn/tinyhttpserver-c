/* webserver.h */
#ifndef WEBSERVER_H
#define WEBSERVER_H

#define MAX_FD           65536
#define MAX_EVENT_NUMBER 1024

void webserver_run(int port, int thread_num);

#endif