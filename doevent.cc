#include "doevent.h"
#include "msg.pb.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <netinet/in.h>

#define ERR_EXIT(info)\
	do\
	{\
		perror(info);\
		exit(EXIT_FAILURE);\
	} while(0)
using namespace std;

typedef vector<struct epoll_event> EventList;

static void activate_nonblock(int conn);
static int getlocalip(char *ip);
static unsigned short getlocalport(int listenfd);

void do_service(int listenfd)
{
	Data::content msg;
	//for epoll
	vector<int> clients;
	int epollfd;
	epollfd = epoll_create1(EPOLL_CLOEXEC);
	
	struct epoll_event event;
	event.data.fd = listenfd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);

	EventList events(64); //init 64 events

	//client's address
	struct sockaddr_in peeraddr;
	socklen_t len = sizeof(peeraddr);

	//accept connect from client
	int conn;
	int i;

	char recvbuf[1024] = {0};
	char sendbuf = '0';

	int nready;
	int ret;
	while(1)
	{
		nready = epoll_wait(epollfd,&*events.begin(),static_cast<int>(events.size()),-1);
		if (nready == -1)
		{
			if (errno == EINTR)
					continue;
			ERR_EXIT("epoll_wait");
		}

		if (nready == 0)
				continue;

		if ((size_t)nready == events.size())
				events.resize(events.size()*2);
		
		for (i = 0; i < nready; ++i)
		{
			//client wants to connect
			if (events[i].data.fd == listenfd)
			{
				if ((conn = accept(listenfd, (struct sockaddr*)&peeraddr, &len)) == -1)
				{
					ERR_EXIT("accept");
				}

				cout << "client:ip is "<< inet_ntoa(peeraddr.sin_addr)<<"port is "<< ntohs(peeraddr.sin_port)<<" connected."<< endl; 
				
				clients.push_back(conn);
				//set nonblock
				activate_nonblock(conn);
				event.data.fd = conn;
				event.events = EPOLLIN | EPOLLET;
				epoll_ctl(epollfd, EPOLL_CTL_ADD, conn, &event);
			}

			//data can be read
			else if (events[i].events & EPOLLIN)
			{
				conn = events[i].data.fd;
				if (conn < 0)
					continue;
				memset(recvbuf, 0, sizeof(recvbuf));
				ret = recv(conn, recvbuf, sizeof(recvbuf), 0);
				if (ret == -1)
					ERR_EXIT("recv");
				
				if (ret == 0)
				{
					struct sockaddr_in clientaddr;
					socklen_t client_len = sizeof(clientaddr);
					if (getpeername(conn,(struct sockaddr*)&clientaddr, &client_len) == -1)
					{
						ERR_EXIT("getpeername");
					}
					cout<<"client ip is "<<inet_ntoa(clientaddr.sin_addr)<<" port is "<<ntohs(clientaddr.sin_port)<<" close."<<endl;

					close(conn);
					event = events[i];
					epoll_ctl(epollfd, EPOLL_CTL_DEL, conn, &event);
					clients.erase(remove(clients.begin(), clients.end(),conn),clients.end());
					continue;

				}

				//decode data
				string data = recvbuf;
				msg.ParseFromString(data);
				cout << "From "<< msg.address()<<" "<<msg.port()<<": ";
				cout << msg.str()<<endl;

				if (send(conn, &sendbuf, strlen(&sendbuf),0) == -1)
				{
					ERR_EXIT("send");
				}
			}

		}

	}
	close(conn);

}

void do_client(int listenfd)
{
	Data::content msg;
	string str;
	string data;
	char sendbuf[1024] = {0};
	char recvbuf = '0';
	char ip[100];
	unsigned short port;
	//get local ip and port
	getlocalip(ip);
	port = getlocalport(listenfd);

	//for select
	fd_set rset;
	FD_ZERO(&rset);

	int nready;
	int maxfd;
	int fd_stdin = fileno(stdin);
	if (fd_stdin > listenfd)
	{
		maxfd = fd_stdin;
	}
	else
	{
		maxfd = listenfd;
	}

	int flag = 1;
	while(1)
	{
		if (flag)
		{
			cout << "input the msg you want to send: ";
			fflush(stdout);
			flag = 0;
		}

		FD_SET(fd_stdin, &rset);
		nready = select(maxfd+1, &rset, NULL, NULL, NULL);
		if (nready == -1)
		{
			ERR_EXIT("select");
		}
		if (nready == 0)
			continue;
		
		if (FD_ISSET(fd_stdin, &rset))
		{
			getline(cin,str);
			memset(sendbuf, 0, sizeof(sendbuf));

			//encode data
			msg.set_port(port);
			msg.set_str(str);
			msg.set_address(ip);
			msg.SerializeToString(&data);
			
			strcpy(sendbuf, data.c_str());
			if (send(listenfd, sendbuf, strlen(sendbuf), 0) == -1)
			{
				ERR_EXIT("send");
			}

			cout << "input the msg you want to send: ";
			fflush(stdout);
		}

		if (FD_ISSET(listenfd, &rset))
		{
			int ret = recv(listenfd, &recvbuf, strlen(&recvbuf), 0);
			if (ret == -1)
			{
				ERR_EXIT("recv");
			}
			else if (ret == 0)
			{
				cout << endl;
				cout << "reminder: server closed." << endl;
				break;
			}
		}
	}


	close(listenfd);
}


//set nonblock
static void activate_nonblock(int conn)
{
	int ret;
	int flag = fcntl(conn, F_GETFL);
	if (flag == -1)
	{
		ERR_EXIT("fcntl get");
	}

	flag |= O_NONBLOCK;
	ret = fcntl(conn, F_SETFL, flag);
	if (ret == -1)
	{
		ERR_EXIT("fcntl set");		
	}
}

//get local ip
static int getlocalip(char *ip)
{
	char host[100] = {0};
	if (gethostname(host, sizeof(host)) < 0)
	{
		ERR_EXIT("gethostname");
	}
	struct hostent *he;
	if ((he = gethostbyname(host)) == NULL)
	{
		ERR_EXIT("gethostbyname");
	}
	strcpy(ip, inet_ntoa(*(struct in_addr*)he->h_addr));
	
	return 0;
}

//get local port
static unsigned short getlocalport(int listenfd)
{
	unsigned short port;
	struct sockaddr_in localaddr;
	socklen_t addrlen = sizeof(localaddr);
	if (getsockname(listenfd, (struct sockaddr*)&localaddr, &addrlen) < 0)
	{
		ERR_EXIT("getsockname");
	}
	
	port = ntohs(localaddr.sin_port);
	return port;
}





