#include "msg.pb.h"
#include "doevent.h"

#include <iostream>
#include <string>
#include <stdio.h>
#include <errno.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define ERR_EXIT(info)\
	do\
	{\
		perror(info);\
		exit(EXIT_FAILURE);\
	} while(0)

//deal with connect in client
extern void do_client(int listenfd);

int main(void)
{
	//socket
	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	if(listenfd == -1)
	{
		ERR_EXIT("socket");
	}
	//the server's address and port
	struct sockaddr_in servaddr;
	memset(&servaddr,0,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(5588);
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	//connect
	if (connect(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr)) == -1)
	{
		ERR_EXIT("connect");
	}
	do_client(listenfd);
	return 0;
}
