#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define MAXEVENTS   512
#define PANDA_PORT 8484

pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;

typedef int socket_t;
typedef int event_fd_t;

struct config {
	char          *model;               // model filename
	int            nr_model_instances;  // number of models to run
	unsigned short panda_port;          // localhost port for the discord bot to connect to
};

static struct config  config;

struct query {
	char     *question;
	char     *answer;
};

struct thread {
	pthread_cond_t   qwait_condition; // thread will wait until a question is asked
	pthread_mutex_t  qwait_mutex;
	struct query    *query;
	char           **usernames;       // keep track of usernames and assign a user to a single thread so that they keep the same context
	int              nr_users;        // number of usernames assigned to this chat model context
	int              busy;
};

static struct thread **threads;

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

void event_add(int epoll_fd, int fd)
{
	struct epoll_event event;
	event.events   = 0;
	event.data.ptr = NULL;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

void event_del(struct connection *connection)
{
	struct epoll_event event;
	event.events   = 0;
	event.data.ptr = NULL;
	epoll_ctl(connection->event.event_fd, EPOLL_CTL_DEL, connection->fd, &event);
}

void panda_get_tokens(int fd)
{
	char buf[256];
	int nbytes;

	nbytes = read(fd, buf, sizeof(buf)-1);
	printf("nbytes: %d\n", nbytes);
	if (nbytes <= 0)
		return;
	printf("buf: %s\n", buf);
}

void load_config()
{
	char config_buf[8192];
	char *p;
	int  fd, nbytes;

	fd = open("config.ini", O_RDONLY);
	if (fd < 0) {
		printf("config.ini is missing\n");
		exit(-1);
	}
	nbytes = read(fd, config_buf, sizeof(config_buf)-1);
	if (nbytes <= 0)
		exit(-1);
	config_buf[nbytes] = 0;

	p = strstr(config_buf, "nr_model_instances");
	if (p) {
		p = strchr(p, '=');
		if (p) {
			p++;
			while (*p == ' ') p++;
		}
		config.nr_model_instances = atoi(p);
	}

	p = strstr(config_buf, "panda_port");
	if (p) {
		p = strchr(p, '=');
		if (p) {
			p++;
			while (*p == ' ') p++;
		}
		config.panda_port = atoi(p);
	}

	p = strstr(config_buf, "model=");
	if (!p)
		p = strstr(config_buf, "model =");
	if (p) {
		p = strchr(p, '=');
		if (p) {
			p++;
			while (*p == ' ') p++;
		}
		config.model = strdup(p);
	}
}

int os_exec_argv(char *argv[], int epoll_fd)
{
	const char *envp[] = { "PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", NULL };
	struct epoll_event events[2];
	char buf[512];
	int status, pid;
	int read_pipe[2], write_pipe[2];

	// Create two pipes for standard input and output of the child process
    if (pipe(read_pipe) < 0 || pipe(write_pipe) < 0) {
        perror("Failed to create pipes");
        exit(EXIT_FAILURE);
    }

	switch ((pid=fork())) {
		case -1:
			exit(-1);
		case 0:
	        close(read_pipe[1]);
	        dup2(read_pipe[0], STDIN_FILENO);
	        close(read_pipe[0]);
	        
	        close(write_pipe[0]);
	        dup2(write_pipe[1], STDOUT_FILENO);
	        close(write_pipe[1]);

			execve(argv[0], argv, (char **)envp);
			exit(-1);
		default:
	        events[0].events  = EPOLLOUT;
	        events[0].data.fd = write_pipe[1];
	        events[1].events = EPOLLIN;
	        events[1].data.fd = read_pipe[0];
	        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, 	write_pipe[1], &events[0]) == -1) {
	            perror("Failed to add write end of pipe to epoll instance");
	            exit(EXIT_FAILURE);
	        }
	        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, read_pipe[0], &events[1]) == -1) {
	            perror("Failed to add read end of pipe to epoll instance");
	            exit(EXIT_FAILURE);
			}
			return write_pipe[1];
	}
}


void *panda_thread(void *args)
{
	struct thread      *thread = (struct thread *)args;
	struct epoll_event  panda_events;
	struct epoll_event *events, *event;
	char                chatbuf[4096];
	char               *argv[] = { "/usr/src/ai/llama.cpp/main",  "--log-disable", "-m", "/usr/src/ai/llama.cpp/openhermes-2.5-mistral-7b.Q4_K_M.gguf", "--color", "--interactive-first", NULL };
	int                 nr_events, fd, epoll_fd, chat_fd, nbytes;

	// open epoll descriptor on the parent thread
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);

	// fork/execve llama.cpp/main and wait for questions from panda.js which will be written to the child's stdin
	chat_fd = os_exec_argv(argv, epoll_fd);
	event_add(epoll_fd, chat_fd);

	events = (struct epoll_event *)malloc(sizeof(*events) * MAXEVENTS);	
	while (1) {
		/*
		 * Await new questions from panda.js (via main())
		 */
		pthread_mutex_lock(&thread->qwait_mutex);
		pthread_cond_wait(&thread->qwait_condition, &thread->qwait_mutex);
		printf("woken up thread\n");
		write(chat_fd, thread->query->question, strlen(thread->query->question));				

		while (1) {
			nr_events = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
			for (int x = 0; x<nr_events; x++) {
				event = &events[x];
				fd    = event->data.fd;
				if (event->events & EPOLLIN) {
					panda_get_tokens(event->data.fd);
				} else {
					close(event->data.fd);
					break;
				}
			}
		}
	}
}


int main()
{
	struct sockaddr_in  client_addr;
	struct query       *query;
	char                question_buf[4096];
	pthread_t           tid;
	int                 panda_server_fd, client_fd, nbytes, client_addr_len, nr_threads = 2;

	load_config();
	printf("nr models: %d\n", config.nr_model_instances);
	printf("port: %d\n", config.panda_port);
	printf("model: %s\n", config.model);

	threads = malloc(sizeof(struct thread) * nr_threads);

	for (int x = 0; x<nr_threads; x++) {
		struct thread *thread = malloc(sizeof(*thread));
		memset(thread, 0, sizeof(*thread));
		memset(&thread->qwait_condition, 0, sizeof(pthread_cond_t));
		memset(&thread->qwait_mutex, 0, sizeof(pthread_mutex_t));
		threads[x] = thread;
		pthread_create(&tid, NULL, panda_thread, thread);
	}

	panda_server_fd = net_tcp_bind(inet_addr("127.0.0.1"), config.panda_port);
	while (1) {
		client_fd = accept(panda_server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		nbytes    = read(client_fd, question_buf, sizeof(question_buf)-1);
		printf("question: %s\n", question_buf);
		query     = malloc(sizeof(*query));
		query->question = strdup(question_buf);
		pthread_mutex_lock(&thread_lock);
		for (int x = 0; x<nr_threads; x++) {
			if (!threads[x]->busy) {
				threads[x]->busy  = 1;
				threads[x]->query = query;
				pthread_cond_signal(&threads[x]->qwait_condition);
				break;
			}
		}
	}
}