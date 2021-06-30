#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "pcsa_net.h"
#include "parse.h"
#include "queue.h"
#include "threadpool.h"

/* 
Reference:
https://github.com/mbrossard/threadpool/blob/master/src/threadpool.c
*/

#define MAXBUF 8192

char port[MAXBUF];
char root[MAXBUF];
int numThreads;
int timeout;
char cgiHandler[MAXBUF];
char svcBuf[MAXBUF];
pthread_mutex_t mutex;
typedef struct sockaddr SA;
struct threadpool_t *threadpool;
int is_shutdown = 0;

void fail_exit(char *msg) { fprintf(stderr, "%s\n", msg); exit(-1); }

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
        {"cgiHandler", required_argument, 0, 'c'},
        {0, 0, 0, 0}};

    while ((c = getopt_long(argc, argv, "p:r:n:t:c", long_options, &option_index)) != -1)
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
        case 'c':
            strcpy(cgiHandler, optarg);
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
    else if (code == 408)
        msg = "408 Request Timeout";
    else if (code == 411)
        msg = "411 Length Required";
    else if (code == 500)
        msg = "500 Internet Server Error";
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

int respond_header(int connFd, char *path, int code, int connection)
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

    if (code == 200)
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
    char const *connect = "";
    if (connection == 0) {
        connect = "close";
    }
    else {
        connect = "keep-alive";
    }

    char const *msg = getStatusCode(code);
    sprintf(buf,
            "HTTP/1.1 %s\r\n"
            "Date: %s\r\n"
            "Server: Tiny\r\n"
            "Connection: %s\r\n"
            "Content-length: %ld\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n\r\n",
            msg, time, connect, size, type, mtime);
    write_all(connFd, buf, strlen(buf));
    return 1;
}

void respond_server(int connFd, char *path, int code, int connection)
{
    respond_header(connFd, path, code, connection);
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

int get_content_length(Request *request) {
    for(int i=0; i<request->header_count; i++) {
        if(strcasecmp(request->headers[i].header_name, "content-length") == 0) {
            return atoi(request->headers[i].header_value);
        }
    }
    return 0;
}

/* 
find whether request connection is close or keep-alive 
if keep-alive return 1, else return 0
*/
int check_connection(Request *request) {
    for(int i=0; i<request->header_count; i++) {
        if(strcasecmp(request->headers[i].header_name, "connection") == 0) {
            if (strcasecmp(request->headers[i].header_value, "close") == 0) {
                return 0;
            }
            else {
                return 1;
            }
        }
    }
    return 1;
}

void getEnvInfo(Request *request, char *length, char *type, char *accept, char *referer, char *accept_encoding,
    char *accept_language, char *accept_charset, char *host, char *cookie, char *user_agent, char *connection) {

    for(int i=0; i<request->header_count; i++) {
        if(strcasecmp(request->headers[i].header_name, "content-length") == 0) {
            strcpy(length, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "content-type") == 0) {
            strcpy(type, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "accept") == 0) {
            strcpy(accept, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "referer") == 0) {
            strcpy(referer, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "accept-encoding") == 0) {
            strcpy(accept_encoding, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "accept-language") == 0) {
            strcpy(accept_language, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "accept-charset") == 0) {
            strcpy(accept_charset, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "host") == 0) {
            strcpy(host, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "cookie") == 0) {
            strcpy(cookie, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "user-agent") == 0) {
            strcpy(user_agent, request->headers[i].header_value);
        }
        else if(strcasecmp(request->headers[i].header_name, "connection") == 0) {
            strcpy(connection, request->headers[i].header_value);
        }
    }
}

/* set environment */
void setEnv(Request *request) {
    char content_length[MAXBUF];
    char content_type[MAXBUF];
    char *query_string = strchr(request->http_uri, '?');
    char path[MAXBUF];
    strcpy(path, request->http_uri);
    char *path_info = strtok(path, "?");

    char http_accept[MAXBUF];
    char referer[MAXBUF];
    char accept_encoding[MAXBUF];
    char accept_language[MAXBUF];
    char accept_charset[MAXBUF];
    char http_host[MAXBUF];
    char cookie[MAXBUF];
    char user_agent[MAXBUF];
    char connection[MAXBUF];
    getEnvInfo(request, content_length, content_type, http_accept, referer, accept_encoding, accept_language, 
    accept_charset, http_host, cookie, user_agent, connection);

    setenv("CONTENT_LENGTH", content_length, 1);
    setenv("CONTENT_TYPE", content_type, 1);
    setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    setenv("PATH_INFO", path_info, 1);
    if(query_string) {
        setenv("QUERY_STRING", query_string+1, 1);
    }
    setenv("REMOTE_ADDR", svcBuf, 1);
    setenv("REQUEST_METHOD", request->http_method, 1);
    setenv("REQUEST_URI", request->http_uri, 1);
    setenv("SCRIPT_NAME", cgiHandler, 1);
    setenv("SERVER_PORT", port, 1);
    setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
    setenv("SERVER_SOFTWARE", "Tiny", 1);
    setenv("HTTP_ACCEPT", http_accept, 1);
    setenv("HTTP_REFERER", referer, 1);
    setenv("HTTP_ACCEPT_ENCODING", accept_encoding, 1);
    setenv("HTTP_ACCEPT_LANGUAGE", accept_language, 1);
    setenv("HTTP_ACCEPT_CHARSET", accept_charset, 1);
    setenv("HTTP_HOST", http_host, 1);
    setenv("HTTP_COOKIE", cookie, 1);
    setenv("HTTP_USER_AGENT", user_agent, 1);
    setenv("HTTP_CONNECTION", connection, 1);
}

void cgi_process(Request *request, int connFd, int length, int method, char *post_data) {
    int c2pFds[2]; /* Child to parent pipe */
    int p2cFds[2]; /* Parent to child pipe */
    if (pipe(c2pFds) < 0) fail_exit("c2p pipe failed.");
    if (pipe(p2cFds) < 0) fail_exit("p2c pipe failed.");

    int pid = fork();

    if (pid < 0) fail_exit("Fork failed.");
    if (pid == 0) { /* Child - set up the conduit & run inferior cmd */

        /* Wire pipe's incoming to child's stdin */
        /* First, close the unused direction. */
        if (close(p2cFds[1]) < 0) fail_exit("failed to close p2c[1]");
        if (p2cFds[0] != STDIN_FILENO) {
            if (dup2(p2cFds[0], STDIN_FILENO) < 0)
                fail_exit("dup2 stdin failed.");
            if (close(p2cFds[0]) < 0)
                fail_exit("close p2c[0] failed.");
        }

        /* Wire child's stdout to pipe's outgoing */
        /* But first, close the unused direction */
        if (close(c2pFds[0]) < 0) fail_exit("failed to close c2p[0]");
        if (c2pFds[1] != STDOUT_FILENO) {
            if (dup2(c2pFds[1], STDOUT_FILENO) < 0)
                fail_exit("dup2 stdin failed.");
            if (close(c2pFds[1]) < 0)
                fail_exit("close pipeFd[0] failed.");
        }

        setEnv(request);
        char* inferiorArgv[] = {cgiHandler, NULL};
        if (execvpe(inferiorArgv[0], inferiorArgv, environ) < 0) {
            respond_header(connFd, NULL, 500, 0);
            fail_exit("exec failed.");
        }
    }
    else { /* Parent - send a random message */

        /* Close the write direction in parent's incoming */
        if (close(c2pFds[1]) < 0) fail_exit("failed to close c2p[1]");

        /* Close the read direction in parent's outgoing */
        if (close(p2cFds[0]) < 0) fail_exit("failed to close p2c[0]");

        /* Write a message to the child - replace with write_all as necessary */
        if(method) {
            size_t len = 0;
            if(post_data != NULL) {
                len = strlen(post_data);
                write_all(p2cFds[1], post_data, sizeof(post_data));
            }
            char buffer[MAXBUF];
            ssize_t toRead = length - len;
            while(toRead > 0) {
                ssize_t readRet = read(connFd, buffer, toRead);
                toRead -= readRet;
                write_all(p2cFds[1], buffer, readRet);
                memset(buffer, 0, sizeof(buffer));
            }
            printf("finish writing\n");
        }

        /* Close this end, done writing. */
        if (close(p2cFds[1]) < 0) fail_exit("close p2c[01] failed.");

        char buf[MAXBUF];
        ssize_t numRead;
        /* Begin reading from the child and writes to the connFd */
        while ((numRead = read(c2pFds[0], buf, MAXBUF))>0) {
            write_all(connFd, buf, numRead);
        }

        /* Close this end, done reading. */
        if (close(c2pFds[0]) < 0) fail_exit("close c2p[01] failed.");

        /* Wait for child termination & reap */
        int status;

        if (waitpid(pid, &status, 0) < 0) fail_exit("waitpid failed.");
        printf("Child exited... parent's terminating as well.\n");
    }    
}

int serve_cgi(Request *request, int connFd, char *post_data) {

    int length = get_content_length(request);
    if (strcasecmp(request->http_method, "GET") == 0)
    {
        cgi_process(request, connFd, length, 0, post_data);
    }
    else if (strcasecmp(request->http_method, "HEAD") == 0)
    {
        cgi_process(request, connFd, length, 0, post_data);
    }
    else if (strcasecmp(request->http_method, "POST") == 0)
    {
        if(!length) {
            respond_header(connFd, NULL, 411, 0);
        }
        else {
            cgi_process(request, connFd, length, 1, post_data);
        }
    }
    else
    {
        respond_header(connFd, NULL, 501, 0);
    }
    return 0;
}

int serve_http(Request *request, int connFd) {
    int connection = 0;
    char path[MAXBUF];
    strcpy(path, root);
    strcat(path, request->http_uri);
    if(strcmp(request->http_uri, "/") == 0) {
        strcat(path, "index.html");
    }

    printf("LOG: %s %s connFd: %d Connection: %d\n", request->http_method, path, connFd, connection);
    if (strcasecmp(request->http_method, "GET") == 0)
    {
        connection = check_connection(request);
        respond_server(connFd, path, 200, connection);
    }
    else if (strcasecmp(request->http_method, "HEAD") == 0)
    {
        connection = check_connection(request);
        respond_header(connFd, path, 200, connection);
    }
    else
    {
        respond_header(connFd, NULL, 501, 0);
    }
    return connection;
}


void parse_request(int connFd, char *rootFolder)
{
    char buf[MAXBUF]; char buffer[MAXBUF];
    memset(buf, 0, sizeof(buf));
    memset(buffer, 0, sizeof(buffer));
    ssize_t readRet = 0;
    ssize_t numRead;
    struct pollfd pfds[1];

    while(1) {
        pfds[0].fd = connFd;
        pfds[0].events = POLLIN;
        int j = poll(pfds, 1, timeout*1000);
        if (j < 0) {
            respond_header(connFd, NULL, 400, 0);
            break;
        }
        if (j == 0) {
            respond_header(connFd, NULL, 408, 0);
            break;
        }
        if ((numRead = read(connFd, buffer, MAXBUF)) > 0)
        {
            readRet += numRead;
            if (readRet > MAXBUF)
            {
                respond_header(connFd, NULL, 400, 0);
                break;
            }
            strcat(buf, buffer);
            /* if there is CRLFCRLF state, then we parse */
            if (strstr(buf, "\r\n\r\n") != NULL) {
                pthread_mutex_lock(&mutex);
                Request *request = parse(buf, readRet, connFd);
                pthread_mutex_unlock(&mutex);
                char *post_data = strstr(buf, "\r\n\r\n");
                if (post_data) {
                    post_data = post_data + 4;
                }
                int connection = 0;
                if (request != NULL) {
                    /* 505 HTTP Version Not Supported */
                    if (strcasecmp(request->http_version, "HTTP/1.1"))
                    {
                        respond_header(connFd, NULL, 505, 0);
                    }
                    else if((strncasecmp(request->http_uri, "/cgi/", 5) == 0)) {
                        connection = serve_cgi(request, connFd, post_data);
                    }
                    else {
                        connection = serve_http(request, connFd);
                    }
                    free(request->headers);
                    free(request);
                }
                else {
                    respond_header(connFd, NULL, 400, 0);
                }
                memset(buf, 0, sizeof(buf));
                readRet = 0;
                if (connection == 0) break; /* Connection: close */
            }
            memset(buffer, 0, sizeof(buffer));
        }
    }
}

void* do_work(void *arg) {

    struct threadpool_t *pool = (struct threadpool_t *) arg;
    pthread_detach(pthread_self());
    for (;;) {
        pthread_mutex_lock(&(pool->jobs_mutex));
        while(isEmpty(&(pool->jobs))) {
            pthread_cond_wait(&(pool->jobs_cond), &(pool->jobs_mutex));
        }

        int connFd = pop(&(pool->jobs));
        if(connFd < 0) {
            break;
        }
        pthread_mutex_unlock(&(pool->jobs_mutex));
        parse_request(connFd, root);
        close(connFd);
        /* Option 5: Go to sleep until it's been notified of changes in the
         * work_queue. Use semaphores or conditional variables
         */
    }
    pthread_mutex_unlock(&(pool->jobs_mutex));
    return NULL;
}

void sigint_handler(int signum)
{
    is_shutdown = 1;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    struct sigaction action;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_handler = sigint_handler;
    sigaction(SIGINT, &action, NULL);

    getOption(argc, argv);
    if (strlen(port) == 0 || strlen(root) == 0 || numThreads <= 0 || timeout <= 0 || strlen(cgiHandler) == 0)
    {
        printf("Required option --port, --root, -- numThreads, --timeout, and --cgiHandler\n");
        exit(0);
    }

    threadpool = threadpool_create(numThreads);
    pthread_mutex_init(&mutex, NULL);

    int listenFd = open_listenfd(port);
    for (;;)
    {
        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);

        int connFd = accept(listenFd, (SA *)&clientAddr, &clientLen);
        if (connFd < 0)
        {
            if (is_shutdown == 1) {
                for(int i=0; i<numThreads; i++) {
                    threadpool_add(threadpool, -1);
                }
                break;
            }
            fprintf(stderr, "Failed to accept\n");
            sleep(1);
            continue;
        }

        char hostBuf[MAXBUF];
        if (getnameinfo((SA *)&clientAddr, clientLen,
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0) == 0)
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");

        threadpool_add(threadpool, connFd);
    }

    if(threadpool->threads) {
        free(threadpool->threads);
        pthread_mutex_lock(&(threadpool->jobs_mutex));
        pthread_mutex_destroy(&(threadpool->jobs_mutex));
        pthread_cond_destroy(&(threadpool->jobs_cond));
    }
    pthread_mutex_destroy(&mutex);
    free(threadpool);
    printf("exit...\n");
    return 0;
}