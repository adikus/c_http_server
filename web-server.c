#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <regex.h>

// Constants
#define QUEUE_SIZE 100
#define PORT 8080
#define BUFLEN 10000
#define N_THREADS 10
#define DEVEL 1 // Development mode

// Global variables
int fd;
regex_t header_regex;
char hostname[100];

/********************************************************************
 * QUEUE Definitions and Methods
 *******************************************************************/

struct circ_queue {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned long count;
    unsigned long size;
    unsigned long in;
    unsigned long out;
    int *buffer;
};
typedef struct circ_queue CQ;

/*
 * create a bounded buffer of integers
 * return NULL if unable to create
 */
CQ *cq_create() {
    int *buf;
    CQ *p = NULL;
    if ((buf = (int *)malloc(QUEUE_SIZE * sizeof(int))) != NULL) {
        if ((p = (CQ *)malloc(sizeof(CQ))) != NULL) {
            p->count = 0;
            p->size = QUEUE_SIZE;
            p->in = 0;
            p->out = 0;
            p->buffer = buf;
            pthread_mutex_init(&(p->lock), NULL);
            pthread_cond_init(&(p->cond), NULL);
        } else {
            free((void *)buf);
        }
    }
    return p;
}

/*
 * put an integer into the bounded buffer; block until there is space
 * returns 1 if put was successful, returns 0 if not
 */
int cq_put(CQ *cq, int item) {
    int result;

    pthread_mutex_lock(&(cq->lock));
    while (cq->count == cq->size) {
        pthread_cond_wait(&(cq->cond), &(cq->lock));
    }
    (cq->buffer)[cq->in] = item;
    cq->in = (cq->in + 1) % (cq->size);
    cq->count++;
    result = 1;
    pthread_cond_signal(&(cq->cond));
    pthread_mutex_unlock(&(cq->lock));
    return result;
}

/*
 * get an integer from the bounded buffer; block until there is an
 * item returns 1 if get was successful, in which case retrieved 
 * item is in *data, return 0 if not
 */
int cq_get(CQ *cq, int *data) {
    int result;

    pthread_mutex_lock(&(cq->lock));
    while (cq->count == 0) {
        pthread_cond_wait(&(cq->cond), &(cq->lock));
    }
    *data = (cq->buffer)[cq->out];
    cq->out = (cq->out + 1) % (cq->size);
    cq->count--;
    result = 1;
    pthread_cond_broadcast(&(cq->cond));
    pthread_mutex_unlock(&(cq->lock));
    return result;
}

/********************************************************************
 * HTTP Headers parsing methods
 *******************************************************************/

struct http_request {
    char *method;
    char *path;
    char *host;
    int done;
};
typedef struct http_request HR;

HR *hr_create() {
    HR *p = NULL;
    if ((p = (HR *)malloc(sizeof(HR))) != NULL) {
        p->done = 0;
        p->method = NULL;
        p->path = NULL;
        p->host = NULL;
        return p;
    }
    return NULL;
}

int hr_set_method(HR *hr, char *method) {
    hr->method = (char *)malloc(sizeof(char) * (strlen(method) + 1));
    if(hr->method == NULL)
        return 0;
    strcpy(hr->method, method);
    return 1;
}

int hr_set_path(HR *hr, char *path) {
    hr->path = (char *)malloc(sizeof(char) * (strlen(path) + 1));
    if(hr->path == NULL)
        return 0;
    strcpy(hr->path, path);
    return 1;
}

int hr_set_host(HR *hr, char *host) {
    hr->host = (char *)malloc(sizeof(char) * (strlen(host) + 1));
    if(hr->host == NULL)
        return 0;
    strcpy(hr->host, host);
    return 1;
}

void parse_first_header_line(char *line, HR *hr) {
    char *token;
    char *saved;
    token = strtok_r(line, " ", &saved);
    int i = 0;
    while( token != NULL ) {
        if(i == 0){
            hr_set_method(hr, token);
        }else if(i == 1){
            hr_set_path(hr, token);
        }
        token = strtok_r(NULL, " ", &saved);
        i++;
    }
}

void parse_headers_line(char *line, HR *hr){
    regmatch_t pmatches[3];
    char *matches[3];
    
    if (regexec(&header_regex, line, 3, pmatches, 0)== 0) {
        int i;
        for(i=0;i<3;i++){
            int len = pmatches[i].rm_eo - pmatches[i].rm_so;
            matches[i] = malloc(sizeof(char) * (len+1));
            memcpy(matches[i], line + pmatches[i].rm_so, len);
            matches[i][len] = 0;
        }
        if(strcmp(matches[1], "Host") == 0){
            hr_set_host(hr, matches[2]);        
        }
    }
}

/********************************************************************
 * Response handling methods
 *******************************************************************/

void get_mime_type(char *file_name, char *type){
    char *token;
    char *saved;
    char extension[10];

    token = strtok_r(file_name, ".", &saved);
    while( token != NULL ) {
        if(strlen(token) < 10)
            strcpy(extension, token);
        token = strtok_r(NULL, ".", &saved);
    }

    if(strcmp(extension, "css") == 0){
        strcpy(type, "text/css");
    }else if(strcmp(extension, "js") == 0){
        strcpy(type, "application/javascript");
    }else if(strcmp(extension, "png") == 0){
        strcpy(type, "image/png");
    }else if(strcmp(extension, "mp3") == 0){
        strcpy(type, "audio/mpeg");
    }else if(strcmp(extension, "html") == 0 || 
        strcmp(extension, "htm") == 0){
        strcpy(type, "text/html");
    }else if(strcmp(extension, "txt") == 0){
        strcpy(type, "text/plain");
    }else if(strcmp(extension, "jpeg") == 0 || 
        strcmp(extension, "jpg") == 0){
        strcpy(type, "image/jpeg");
    }else if(strcmp(extension, "gif") == 0){
        strcpy(type, "image/gif");
    }else{
        strcpy(type, "application/octet-stream");
    }
}

int check_hostname(char *req_hostname){
    char target_name[100];
    if(strcmp(hostname, "andrej") == 0){
        // We are on my machine, allow only localhost
        sprintf(target_name, "localhost:%d", PORT);
        return strcmp(req_hostname, target_name) == 0;
    }else{
        //We are in the lab
        sprintf(target_name, "%s:%d", hostname, PORT);
        char alt_target_name[100];
        sprintf(alt_target_name, "%s.dcs.gla.ac.uk:%d", hostname, PORT);
        return strcmp(req_hostname, target_name) == 0 ||
            strcmp(req_hostname, alt_target_name) == 0;
    }
}

void get_file_name_from_path(char *path, char *fileName){
    strncpy(fileName, path + 1, strlen(path));
    int len = strlen(fileName);
    if(len == 0){
        strcpy(fileName, ".");
    }
}

void construct_headers(char* headers, char *code, char *mime, 
    unsigned int length, HR *hr){
    sprintf(headers, "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
            "Content-Length: %u\r\n\r\n", code, mime, length);
    printf("%s %s %s %s\n", hr->host, code, hr->method, hr->path);
}

void send_headers_with_body(int fd, char *http_code, char *body, HR *hr) {
    char response[500];
    construct_headers(response, http_code, "text/html", strlen(body), hr);
    strcat(response, body);
    write(fd, response, strlen(response));
}

void send_file(int fd, FILE *fp){
    char buf[BUFLEN];
    size_t count;
    long int fpos = 0;

    while(fread(buf, BUFLEN, 1, fp) == 1){
        fpos = ftell(fp);
        write(fd, buf, BUFLEN);
    }
    fseek(fp, fpos, SEEK_SET);
    count = fread(buf, 1, BUFLEN, fp);
    if(count > 0){
        write(fd, buf, count);
    }
}

void send_response(int fd, HR *hr){
    if(check_hostname(hr->host) == 0){
        send_headers_with_body(fd, "400 Bad Request", "Wrong hostname.\n", hr);
        return;
    }
    if(strcmp(hr->method, "GET") != 0){
        send_headers_with_body(fd, "405 Method Not Allowed", 
            "Wrong method.\n", hr);
        return;
    }

    char filename[100];
    FILE *fp;
    struct stat statbuf;

    get_file_name_from_path(hr->path, filename);
    stat(filename, &statbuf);
    
    if(S_ISDIR(statbuf.st_mode)){
        strcat(filename, "/index.html");
        stat(filename, &statbuf);
    }
    
    fp = fopen(filename,"r");
    if(fp != NULL){
        char type[40];
        char headers[500];

        get_mime_type(filename, type);
        construct_headers(headers, "200 OK", type, 
            (unsigned int)statbuf.st_size, hr);
        write(fd, headers, strlen(headers));
    }else{
        send_headers_with_body(fd,"404 Not Found","Not found.\n", hr);
        return;
    }

    send_file(fd, fp);
    fclose(fp);
}

/********************************************************************
 * General server methods
 *******************************************************************/

void process_request(int fd, char *buf, HR *hr) {
    char *token;
    char *saved;
    char line[500];
    int line_n = 0;

    token = strtok_r(buf, "\n", &saved);
    while( token != NULL ) {
        int token_len = strlen(token);
        if(token_len < 500){
            if(token_len == 1){
                hr->done = 1;
                send_response(fd, hr);
                free((void *)hr);
                hr = hr_create();
            }else{
                memset(line, '\0', 500);
                strcpy(line, token);
                if(line_n == 0){
                    parse_first_header_line(line, hr);
                }else{
                    parse_headers_line(line, hr);
                }
            }
        }else{
            printf("Skipping long header line.");
        }
        line_n++;
        token = strtok_r(NULL, "\n", &saved);
    }
}

int start_server(int port) {
    int server_fd;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Error: Unable to create socket.\n");
        return -1;
    }

    struct sockaddr_in addr;

    // If we are in development mode, make the socket reusable
    // This allows to bind the socket right away after restarting 
    // the server
    if(DEVEL){
        int yes = 1;
        if (setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int))==-1){
            perror("setsockopt");
        }
    }

    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        printf("Error: Unable to bind socket.\n");
        return -1;
    }

    int backlog = 10;
    if (listen(server_fd, backlog) == -1) {
        printf("Error: Unable to start listening.\n");
        return -1;
    }

    printf("The server has been started and is listening at %i\n", port);
    return server_fd;
}

void *worker(void *args) {
    CQ *queue = (CQ *)args;
    int fd;
    ssize_t rcount;
    char buf[BUFLEN];
    HR *http_request = hr_create();

    while(1){
        cq_get(queue, &fd);
        printf("Connection accepted.\n");
        while((rcount = read(fd, buf, BUFLEN)) > 0){
            process_request(fd, buf, http_request);
            memset(buf, '\0', BUFLEN);
        }
        if (rcount == -1) {
            printf("Error: Unable to read data from client.\n");
        }
        close(fd);
        printf("Connection with client terminated.\n");
    }
    return NULL;
}

void sigint_handler() {
    printf("\nSIGINT detected. Shutting down server...\n");
    close(fd);
    exit(1);
}

void setup_sigint_handler() {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

int main() {
    pthread_t *threads;
    CQ *queue = cq_create();

    setup_sigint_handler();
    gethostname(hostname, 99);
    if((fd = start_server(PORT)) == -1){
        exit(1);
    }

    if ((threads = (pthread_t *)malloc(N_THREADS * sizeof(pthread_t))) == NULL){
        fprintf(stderr, "Unable to malloc() array of pthread_t's\n");
        exit(1);
    }
    if (regcomp(&header_regex, "(.*): (.*)\\s", REG_EXTENDED | REG_ICASE)) {
        printf("Could not compile header regex.\n");
        exit(1);
    }
    int i;
    for (i = 0; i < N_THREADS; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, worker, (void *)queue) != 0) {
            fprintf(stderr, "Unable to create the %d-th thread\n", i+1);
            exit(1);
        }
        threads[i] = t;
    }
    printf("Workers created\n");

    int connfd;
    while(1) {
        struct sockaddr_in cliaddr;
        socklen_t cliaddr_len = sizeof(cliaddr);
    
        connfd = accept(fd, (struct sockaddr*) &cliaddr, &cliaddr_len);
        if (connfd == -1) {
            printf("Error: Unable to accept connection.\n");
            continue;
        }

        cq_put(queue, connfd);
    }
    return 0;
}
