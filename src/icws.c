#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
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

char* getStatusCode(int code) {
    char* msg = NULL;
    /* respond status code */
    if(code==200) msg = "200 OK";
    else if(code==400) msg = "400 Bad Request";
    else if(code==404) msg = "404 File Not Found";
    else if(code==501) msg = "501 Method Unimplemented";
    else if(code==505) msg = "505 HTTP Version Not Supported";
    return msg;
} 

char* getMIMEType(char *ext) {
    char* type = "";
    /* MIME types */
    if(strcmp(ext, "html")==0) type = "text/html";
    else if(strcmp(ext, "css")==0) type = "text/css";
    else if(strcmp(ext, "txt")==0) type = "text/plain";
    else if(strcmp(ext, "jpg")==0) type = "image/jpg";
    else if(strcmp(ext, "jpeg")==0) type = "image/jpeg";
    else if(strcmp(ext, "gif")==0) type = "image/gif";
    else if(strcmp(ext, "js")==0) type = "application/javascript";
    return type;
}

int respond_header(int connFd, char *path, int code) {
    char buf[MAXBUF];
    struct stat s;
    time_t t = time(NULL); /* time */
    struct tm *tm = localtime(&t);
    char time[64];
    strftime(time, sizeof(time), "%c", tm);
    /* file size, MIME types, time of last modification */
    off_t size = 0; char* type = ""; char mtime[64] = "";

    if(code != 400 && code != 501) {
        if(stat(path, &s)>=0) {
            char* ext = strrchr(path, '.') + 1;
            type = getMIMEType(ext);
            size = s.st_size;
            tm = localtime(&s.st_mtime);
            strftime(mtime, sizeof(mtime), "%c", tm);
        }
        else {
            code = 404;
        }
    }

    char *msg = getStatusCode(code);
    sprintf(buf,
        "HTTP/1.1 %s\r\n"
        "Date: %s\r\n"
        "Server: Tiny\r\n"
        "Connection: close\r\n"
        "Content-length: %ld\r\n"
        "Content-type: %s\r\n"
        "Last-Modified: %s\r\n\r\n", msg, time, size, type, mtime);
    write_all(connFd, buf, strlen(buf));
    return 1;
}

void respond_server(int connFd, char *path, int code) {

    respond_header(connFd, path, code);
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

/* check if it is CRLFCRLF state yet */
int check_state(char *buffer, int size) {
    enum {
		STATE_START = 0, STATE_CR, STATE_CRLF, STATE_CRLFCR, STATE_CRLFCRLF
	};

	int i = 0, state;
	char ch;

	state = STATE_START;
	while (state != STATE_CRLFCRLF) {
		char expected = 0;

		if (i == size)
			break;

		ch = buffer[i++];

		switch (state) {
		case STATE_START:
		case STATE_CRLF:
			expected = '\r';
			break;
		case STATE_CR:
		case STATE_CRLFCR:
			expected = '\n';
			break;
		default:
			state = STATE_START;
			continue;
		}

		if (ch == expected)
			state++;
		else
			state = STATE_START;
	}

    if (state == STATE_CRLFCRLF) {
		return 1;
	}
	return 0;
}

void serve_http(int connFd, char *rootFolder) {
    char buf[MAXBUF];
    char buffer[MAXBUF];
    ssize_t readRet = 0;
    ssize_t numRead;
    /* Drain the remaining of the request */
    while ((numRead = read(connFd, buffer, MAXBUF))>0) {
        readRet += numRead;
        if(readRet > MAXBUF) {
            respond_header(connFd, NULL, 400);
            return ;
        }
        /* if request header is not > 8k bytes */
        strcpy(buf + (readRet-numRead), buffer);
        if (check_state(buf, readRet)) {
            Request *request = parse(buf,readRet,connFd);
            if(request!=NULL) {
                char path[MAXBUF];
                strcpy(path, rootFolder);
                strcat(path, request->http_uri);
                /* 505 HTTP Version Not Supported */
                if(strcasecmp(request->http_version, "HTTP/1.1")) {
                    respond_header(connFd, path, 505);
                }
                else if (strcasecmp(request->http_method, "GET") == 0) {
                    respond_server(connFd, path, 200);
                }
                else if (strcasecmp(request->http_method, "HEAD") == 0) {
                    respond_header(connFd, path, 200);
                }
                else {
                    respond_header(connFd, NULL, 501);
                }     
                free(request->headers);
                free(request);
            }
            /* Malformed Requests */
            else {
                respond_header(connFd, NULL, 400);
            }
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