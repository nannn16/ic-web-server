#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include "simple_work_queue.hpp"
extern "C" {
#include "pcsa_net.h"
#include "parse.h"
}

// https://github.com/mbrossard/threadpool/blob/master/src/threadpool.c

#define MAXBUF 8192

char port[MAXBUF];
char root[MAXBUF];
int numThreads;
int timeout;
typedef struct sockaddr SA;

// struct threadpool_t *threadpool;

/* to help parse the command-line argument */
int getOption(int argc, char **argv)
{

    int option_index = 0;
    int c;

    struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"root", required_argument, 0, 'r'},
        {"numThreads", required_argument, 0, 'n'},
        {"timeout", required_argument, 0, 't'},
        {0, 0, 0, 0}};

    while ((c = getopt_long(argc, argv, "p:r:n:t", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'p':
            strcpy(port, optarg);
            break;
        case 'r':
            strcpy(root, optarg);
            break;
        case 'n':
            numThreads = atoi(optarg);
            break;
        case 't':
            timeout = atoi(optarg);
            break;
        default:
            printf("get unknown option\n");
            exit(0);
        }
    }
    return 1;
}

char const *getStatusCode(int code)
{
    char const *msg = NULL;
    /* respond status code */
    if (code == 200)
        msg = "200 OK";
    else if (code == 400)
        msg = "400 Bad Request";
    else if (code == 404)
        msg = "404 File Not Found";
    else if (code == 501)
        msg = "501 Method Unimplemented";
    else if (code == 505)
        msg = "505 HTTP Version Not Supported";
    return msg;
}

char const *getMIMEType(char *ext)
{
    char const *type = "";
    /* MIME types */
    if (strcmp(ext, "html") == 0 || strcmp(ext, "hml") == 0)
        type = "text/html";
    else if (strcmp(ext, "css") == 0)
        type = "text/css";
    else if (strcmp(ext, "txt") == 0)
        type = "text/plain";
    else if (strcmp(ext, "jpg") == 0)
        type = "image/jpg";
    else if (strcmp(ext, "jpeg") == 0)
        type = "image/jpeg";
    else if (strcmp(ext, "gif") == 0)
        type = "image/gif";
    else if (strcmp(ext, "js") == 0 || strcmp(ext, "mjs") == 0)
        type = "text/javascript";
    return type;
}

int respond_header(int connFd, char *path, int code)
{
    char buf[MAXBUF];
    struct stat s;
    time_t t = time(NULL); /* time */
    struct tm *tm = localtime(&t);
    char time[64];
    strftime(time, sizeof(time), "%c", tm);
    /* file size, MIME types, time of last modification */
    off_t size = 0;
    char const *type = "";
    char mtime[64] = "";

    if (code != 400 && code != 501)
    {
        if (stat(path, &s) >= 0)
        {
            char *ext = strrchr(path, '.') + 1;
            type = getMIMEType(ext);
            size = s.st_size;
            tm = localtime(&s.st_mtime);
            strftime(mtime, sizeof(mtime), "%c", tm);
        }
        else
        {
            code = 404;
        }
    }

    char const *msg = getStatusCode(code);
    sprintf(buf,
            "HTTP/1.1 %s\r\n"
            "Date: %s\r\n"
            "Server: Tiny\r\n"
            "Connection: close\r\n"
            "Content-length: %ld\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n\r\n",
            msg, time, size, type, mtime);
    write_all(connFd, buf, strlen(buf));
    return 1;
}

void respond_server(int connFd, char *path, int code)
{

    respond_header(connFd, path, code);
    char buf[MAXBUF];
    int inputFd = open(path, O_RDONLY);
    if (inputFd <= 0)
    {
        printf("Failed to open the file\n");
        return;
    }

    ssize_t numRead;
    while ((numRead = read(inputFd, buf, MAXBUF)) > 0)
    {
        write_all(connFd, buf, numRead);
    }
    close(inputFd);
}

void serve_http(int connFd, char *rootFolder, threadpool_t *threadpool)
{
    char buf[MAXBUF];
    memset(buf, 0, sizeof(buf));
    char buffer[MAXBUF];
    ssize_t readRet = 0;
    ssize_t numRead;
    /* Drain the remaining of the request */
    while ((numRead = read(connFd, buffer, MAXBUF)) > 0)
    {
        readRet += numRead;
        if (readRet > MAXBUF)
        {
            printf("Request header too large\n");
            respond_header(connFd, NULL, 400);
            return ;
        }
        strcat(buf, buffer);
        /* if there is CRLFCRLF state, then we parse */
        if (strstr(buf, "\r\n\r\n") != NULL) break;
    }

    pthread_mutex_lock(&threadpool->jobs_mutex);
    Request *request = parse(buf, readRet, connFd);
    pthread_mutex_unlock(&threadpool->jobs_mutex);
    printf("nan\n");

    if (request != NULL)
    {
        char path[MAXBUF];
        strcpy(path, rootFolder);
        strcat(path, request->http_uri);

        /* 505 HTTP Version Not Supported */
        if (strcasecmp(request->http_version, "HTTP/1.1"))
        {
            respond_header(connFd, path, 505);
        }
        else if (strcasecmp(request->http_method, "GET") == 0)
        {
            printf("LOG: Sending %s\n", path);
            respond_server(connFd, path, 200);
        }
        else if (strcasecmp(request->http_method, "HEAD") == 0)
        {
            respond_header(connFd, path, 200);
        }
        else
        {
            respond_header(connFd, NULL, 501);
        }
        free(request->headers);
        free(request);
    }
    /* Malformed Requests */
    else
    {
        respond_header(connFd, NULL, 400);
    }
    return ;
}

void* do_work(void *pool) {

    threadpool_t *tpool = (threadpool_t *)pool;

    for (;;) {
        survival_bag context;
        if (tpool->remove_job(&context)) {

            // if (connFd < 0) break; // Terminate with a number < 0
            printf("%d\n", context.connFd);
            serve_http(context.connFd, context.rootFolder, tpool);
            close(context.connFd);
        }
        /* Option 5: Go to sleep until it's been notified of changes in the
         * work_queue. Use semaphores or conditional variables
         */
    }
    return NULL;
}

threadpool_t *threadpool_create(int numThreads) {
    threadpool_t *threadpool;
    threadpool = (threadpool_t *) malloc(sizeof(threadpool_t));

    threadpool->threads = (pthread_t *)malloc(sizeof(pthread_t) * numThreads);

    pthread_mutex_init(&(threadpool->jobs_mutex), NULL);
    pthread_cond_init(&(threadpool->jobs_cond), NULL);

    for (int i=0; i<numThreads; i++) {
        pthread_create(&(threadpool->threads[i]), NULL, do_work, (void*)threadpool);
    }
    return threadpool;
}

int main(int argc, char **argv)
{
    getOption(argc, argv);
    if (strlen(port) == 0 || strlen(root) == 0)
    {
        printf("Required option both --port and --root\n");
        exit(0);
    }

    threadpool_t *threadpool;
    threadpool = threadpool_create(numThreads);

    int listenFd = open_listenfd(port);

    for (;;)
    {
        pthread_t threadInfo;
        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);

        int connFd = accept(listenFd, (SA *)&clientAddr, &clientLen);
        if (connFd < 0)
        {
            fprintf(stderr, "Failed to accept\n");
            sleep(1);
            continue;
        }

        char hostBuf[MAXBUF], svcBuf[MAXBUF];
        if (getnameinfo((SA *)&clientAddr, clientLen,
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0) == 0)
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");

        struct survival_bag context;

        context.connFd = connFd;
        strcpy(context.rootFolder, root);
        // threadpool->add_job(context);
        // threadpool->add_job(connFd);
        serve_http(connFd, root, threadpool);
        close(connFd);
    }
    return 0;
}