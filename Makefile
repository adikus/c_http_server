%.o : %.c
	gcc -c -W -Wall $< -o $@

server: web-server.o
	gcc -pthread -W -Wall -Werror web-server.o -o web-server

http_server.o: web-server.c