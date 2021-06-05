#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include "parse.h"
#include "pcsa_net.h"

#define MAXBUF 8192

char port[MAXBUF];
char root[MAXBUF];
typedef struct sockaddr SA;

/* to help parse the command-line argument */
int getOption(int argc, char **argv) {

    int option_index = 0;
    int c;

    struct option long_options[] = {
        { "port", required_argument, 0, 'p' },
        { "root", required_argument, 0, 'r' },
        {0, 0, 0, 0}
    };

    while((c = getopt_long(argc, argv, "p:r", long_options, &option_index))!=-1) {
        switch (c) {
            case 'p': strcpy(port,optarg); break;
            case 'r': strcpy(root,optarg); break;
            default:
                printf("get unknown option\n");
                exit(0);
        }
    }
    return 1;
}

int respond_header(int connFd, char *path) {

    char buf[MAXBUF];
    struct stat s;
    char* type = "";
    char* ext = strrchr(path, '.');
    ext = ext+1;

    if(stat(path, &s)>=0) {
        if(s.st_size==0) {
            char * msg = "411 Length Required\n";
            write_all(connFd, msg , strlen(msg) );
            return 0;
        } 

        if(strcmp(ext, "html")==0) type = "text/html";
        else if(strcmp(ext, "jpg")==0) type = "image/jpg";
        else if(strcmp(ext, "jpeg")==0) type = "image/jpeg";

        sprintf(buf,
            "HTTP/1.1 200 OK\r\n"
            "Server: Tiny\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n\r\n", s.st_size, type);
        write_all(connFd, buf, strlen(buf));
    }
    else {
        char * msg = "404 Not Found\n";
        write_all(connFd, msg , strlen(msg) );
        return 0;
    }
    return 1;
}

void respond_server(int connFd, char *path) {

    if(!respond_header(connFd, path)) {
        return ;
    }
    char buf[MAXBUF];
    int inputFd = open(path, O_RDONLY);
    if(inputFd <= 0) {
        printf("Failed to open the file\n");
        return ;
    }
    
    ssize_t numRead;
    while((numRead = read(inputFd,buf,MAXBUF))>0) {
        write_all(connFd, buf, numRead);
    }
    close(inputFd);
}

void serve_http(int connFd, char *rootFolder) {
    char buf[MAXBUF];
    int readRet;
    /* Drain the remaining of the request */
    if ((readRet = read(connFd, buf, MAXBUF)) < MAXBUF) {
        // if (strcmp(buf, "\r\n") == 0) break;
        Request *request = parse(buf,readRet,connFd);

        if(request!=NULL) {
            char path[MAXBUF];
            strcpy(path, rootFolder);
            strcat(path, request->http_uri);

            if (strcasecmp(request->http_method, "GET") == 0) {
                printf("LOG: Sending %s\n", path);
                respond_server(connFd, path);
            }
            else if (strcasecmp(request->http_method, "HEAD") == 0) {
                respond_header(connFd, path);
            }
            else {
                char * msg = "501 Method Unimplemented\n";
                write_all(connFd, msg , strlen(msg));
                return ;
            }     
            free(request->headers);
            free(request);
        }
        /* Malformed Requests */
        else {
            char * msg = "404 Not Found\n";
            write_all(connFd, msg , strlen(msg));
            return ;
        }
    }   
}

int main(int argc, char **argv){

    getOption(argc, argv);

    if(strlen(port) == 0 || strlen(root) == 0) {
        printf("Required option both --port and --root\n");
        exit(0);
    }

    int listenFd = open_listenfd(port);

    for (;;) {
        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);

        int connFd = accept(listenFd, (SA *) &clientAddr, &clientLen);
        if (connFd < 0) { 
            fprintf(stderr, "Failed to accept\n"); 
            sleep(1);
            continue; 
        }

        char hostBuf[MAXBUF], svcBuf[MAXBUF];
        if (getnameinfo((SA *) &clientAddr, clientLen, 
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0)==0) 
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");
        serve_http(connFd, root);
        close(connFd);
    }

    return 0;
}