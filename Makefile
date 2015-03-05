%.o : %.c
	gcc -c -W -Wall $< -o $@

server: http_server.o
	gcc -pthread -W -Wall -Werror http_server.o -o http_server

http_server.o: http_server.c