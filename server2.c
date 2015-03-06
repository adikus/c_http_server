#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/select.h>

#define BUFLEN 10000

int fd;

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

int start_server(int port) {
	int server_fd;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Error: Unable to create socket.\n");
		return -1;
	}

	struct sockaddr_in addr;

	int yes = 1;
	if ( setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1 ){
		perror("setsockopt");
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

	printf("\e[1;32mThe glorious server has been started and is listening port on \e[1;31m%i\e[0m\n", port);

	return server_fd;
}

void get_type(char *file_name, char *type){
	char *token;
	char *saved;
	char extension[10];

	token = strtok_r(file_name, ".", &saved);
	while( token != NULL ) {
		if(strlen(token) < 10){
			strcpy(extension, token);
		}
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
	}else{
		strcpy(type, "text/html");
	}
}

void send_response(int connfd, char* requested_file) {
	FILE *fp;
	char headers[200];

	struct stat statbuf;
	stat(requested_file, &statbuf);

	fp = fopen(requested_file,"r");
	if(fp != NULL){
		printf("200 %s\n", requested_file);
		
		char type[40];
		get_type(requested_file, type);
		
		
		char size_s[30];
		sprintf(size_s, "%d", (int)statbuf.st_size);

		strcpy(headers, "HTTP/1.1 200 OK\r\nContent-Type: ");
		strcat(headers, type);
		strcat(headers, "\r\nContent-Length: ");
		strcat(headers, size_s);
		strcat(headers, "\r\n\r\n");
		write(connfd, headers, strlen(headers));
		printf("Headers sent (Size: %s, Type: %s)\n", size_s, type);
	}else{
		printf("404 %s\n", requested_file);
		strcpy(headers, "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\nNot found.\n");
		write(connfd, headers, strlen(headers));
		return;
	}

	char buf[BUFLEN];
	size_t count;
	long int fpos = 0;

	while(fread(buf, BUFLEN, 1, fp) == 1){
		fpos = ftell(fp);
		write(connfd, buf, BUFLEN);
	}
	fseek(fp, fpos, SEEK_SET);
	printf("Sent %li bytes of file\n", fpos);
	count = fread(buf, 1, BUFLEN, fp);
	if(count > 0){
		write(connfd, buf, count);
	}
	printf("Sent another %u bytes of file\n", (unsigned int)count);
	fclose(fp);
}

void parse_headers_line(char *line, char *host, char *method, char *path) {
	//printf("%s\n", line);
	char *token;
	char *saved;
	if(strlen(method) > 0){
		token = strtok_r(line, ": ", &saved);
		int i = 0;
		while( token != NULL ) {
			if(i == 0){
				//printf("%s\n", token);
			}/*else if(i == 1){

			}*/
			token = strtok_r(NULL, ": ", &saved);
			i++;
		}
	}else{
		token = strtok_r(line, " ", &saved);
		int i = 0;
		while( token != NULL ) {
			if(i == 0){
				memset(method, '\0', strlen(token));
				strcpy(method, token);
			}else if(i == 1){
				memset(path, '\0', strlen(token));
				strcpy(path, token);
			}
			token = strtok_r(NULL, " ", &saved);
			i++;
		}
	}
}

void getFileNameFromPath(char *path, char *fileName){
	strncpy(fileName, path + 1, strlen(path));
	int len = strlen(fileName);
	if(len == 0){
		strcpy(fileName, "index.html");
	}
}

void parse_headers(char *headers, char *fileName) {
	char *token;
	char *saved;

	char host[50];
	char method[10];
	char path[100];
	char line[500];

	host[0] = method[0] = path[0] = '\0';

	token = strtok_r(headers, "\r\n", &saved);
	while( token != NULL ) {
		if(strlen(token) < 500){
			memset(line, '\0', 500);
			strcpy(line, token);
			parse_headers_line(line, host, method, path);
		}else{
			printf("Skipping long header line.");
		}
		token = strtok_r(NULL, "\r\n", &saved);
	}
	printf("host: %s method: %s path: %s\n", host, method, path);

	getFileNameFromPath(path, fileName);
}

void accept_connection() {
	int connfd;
	struct sockaddr_in cliaddr;
	socklen_t cliaddr_len = sizeof(cliaddr);

	ssize_t rcount;
	char buf[BUFLEN];

	connfd = accept(fd, (struct sockaddr*) &cliaddr, &cliaddr_len);
	if (connfd == -1) {
		printf("Error: Unable to accept connection.\n");
		return;
	}

	int pid = fork();

	if(pid == 0){
        printf("\e[0;33mConnection accepted.\e[0m\n");

        fd_set rfds;
		struct timeval timeout;
		int rc;

		while(1){
			timeout.tv_sec = 1; // 1 second timeout
			timeout.tv_usec = 0;
			FD_ZERO(&rfds);
			FD_SET(connfd, &rfds);

			rc = select(connfd + 1, &rfds, NULL, NULL, &timeout);
			if(rc > 0){
				rcount = read(connfd, buf, BUFLEN);
				if (rcount == -1) {
					printf("Error: Unable to read data from client.\n");
				}

				printf("%i Bytes of data received..\n", (int)rcount);
				char requested_file[100];
				parse_headers(buf, requested_file);
				send_response(connfd, requested_file);
				printf("Response sent...\n");
				memset(buf, '\0', BUFLEN);
			}else{
				break;
			}
		}

		close(connfd);

		printf("Connection with client terminated.\n");
		exit(0);
    }
}

int main() {
	setup_sigint_handler();

	if((fd = start_server(5000)) == -1){
		exit(1);
	}

	while(1) {
		accept_connection();
	}

	return 0;
}
