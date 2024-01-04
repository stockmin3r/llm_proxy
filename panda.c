#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define MAXEVENTS   512
#define PANDA_PORT 8484

typedef int socket_t;
typedef int event_fd_t;

struct event {
	event_fd_t    event_fd;
};

struct connection {
	struct event  event;
	int           fd;
	unsigned int  events;
};

socket_t net_tcp_bind(uint32_t bind_addr, unsigned short port)
{
	struct sockaddr_in serv;
	int sockfd, val = 1;

	sockfd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, IPPROTO_TCP);
	if (sockfd < 0)
		return -1;

	setsockopt(sockfd, SOL_SOCKET,  SO_REUSEADDR, (const char *)&val, sizeof(val));
	setsockopt(sockfd, SOL_SOCKET,  SO_REUSEPORT, (const char *)&val, sizeof(val));
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,  (const char *)&val, sizeof(val));

	serv.sin_family      = AF_INET;
	serv.sin_port        = htons(port);
	serv.sin_addr.s_addr = bind_addr;
	if (bind(sockfd, (struct sockaddr *)&serv, 0x10) < 0) {
		close(sockfd);
		return -1;
	}
	if (listen(sockfd, 15) < 0) {
		close(sockfd);
		return -1;
	}
	return (sockfd);
}

int net_tcp_connect(const char *dst_addr, unsigned short dst_port)
{
	struct sockaddr_in paddr;
	int dst_fd;

	dst_fd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, IPPROTO_TCP);
	if (dst_fd < 0) {
		perror("socket");
		return -1;
	}
	paddr.sin_family      = AF_INET;
	paddr.sin_port        = htons(dst_port);
	paddr.sin_addr.s_addr = inet_addr(dst_addr);
	if (connect(dst_fd, (struct sockaddr *)&paddr, 0x10) < 0) {
		close(dst_fd);
		return -1;
	}
	return (dst_fd);
}

void net_socket_nonblock(socket_t sockfd)
{
	int val = fcntl(sockfd, F_GETFL);
	fcntl(sockfd, F_SETFL, val|O_NONBLOCK);
}

void event_mod(struct connection *connection)
{
	struct epoll_event event;
	event.events   = connection->events;
	event.data.ptr = (void *)connection;
	epoll_ctl(connection->event.event_fd, EPOLL_CTL_MOD, connection->fd, &event);
}

void event_del(struct connection *connection)
{
	struct epoll_event event;
	event.events   = 0;
	event.data.ptr = NULL;
	epoll_ctl(connection->event.event_fd, EPOLL_CTL_DEL, connection->fd, &event);
}


void os_exec_argv(char *argv[], int epoll_fd)
{
	const char *envp[] = { "PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", NULL };
	char buf[512];
	int status, pid;
	int pfd[2];

	pipe(pfd);        
	switch ((pid=fork())) {
		case 0:
			close(pfd[0]);
			dup2(pfd[1], 1);
			dup2(pfd[1], 2);
			close(pfd[1]);
				execve(argv[0], argv, (char **)envp);
				exit(-1);
                default:
			while (read(pfd[0], buf, sizeof(buf)-1) != 0)
				printf("%s", buf);
			waitpid(pid, &status, 0);
	}
}

void panda_process_message(struct connection *connection)
{
	char buf[256];
	int nbytes;

	nbytes = read(connection->fd, buf, sizeof(buf)-1);
	printf("nbytes: %d\n", nbytes);
	if (nbytes <= 0)
		return;
	printf("buf: %s\n", buf);
}

int main()
{
	char               *argv[] = { "/usr/bin/git", "ls-remote", "https://github.com/stockmin3r/stockminer.git", "--verify", "HEAD", NULL };
	struct epoll_event  panda_events;
	struct epoll_event *events, *event;
	struct sockaddr_in  client_addr;
	struct connection  *connection;
	in_addr_t           ipaddr;
	int                 nr_events, panda_server_fd, client_addr_len, client_fd, fd, epoll_fd;

	epoll_fd        = epoll_create1(EPOLL_CLOEXEC);
	panda_server_fd = net_tcp_bind(ipaddr, PANDA_PORT);
	net_socket_nonblock(panda_server_fd);

	panda_events.events  = EPOLLIN | EPOLLET | EPOLLHUP;
	panda_events.data.fd = panda_server_fd;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, panda_server_fd, &panda_events);
	events = (struct epoll_event *)malloc(sizeof(*events) * MAXEVENTS);

	os_exec_argv(argv, epoll_fd);

	while (1) {
		nr_events = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
		for (int x = 0; x<nr_events; x++) {
			event = &events[x];
			fd    = event->data.fd;
			if (fd == panda_server_fd) {
				client_fd = accept4(panda_server_fd, (struct sockaddr *)&client_addr, &client_addr_len, O_NONBLOCK);
				if (client_fd < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						continue;
				}
				connection = malloc(sizeof(*connection));
				event->events    = EPOLLIN|EPOLLET;
				event->data.ptr  = (void *)connection;
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, event);
			} else if (event->events & EPOLLIN) {
				/* Connection Event recv() */
				connection = (struct connection *)event->data.ptr;
				panda_process_message(connection);
				printf("EPOLLIN fd: %d epoll_fd: %d\n", connection->fd, epoll_fd);
			} else if (event->events & EPOLLRDHUP) {
				close(event->data.fd);
				continue;
			}
		}
	}

}
