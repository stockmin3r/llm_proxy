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

#define CONFIG_FILE     "config.ini"
#define MAX_CONFIG_LINE 1024

#define MAXEVENTS       512
#define PANDA_PORT      8484

#define EVENT_READ      EPOLLIN
#define EVENT_WRITE     EPOLLOUT

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
	char     *id;                     // question ID (used by discord.js to lookup the message object)
	char     *answer;
	int       nr_tokens;              // current number of tokens
};

struct thread {
	pthread_cond_t   qwait_condition; // thread will wait until a question is asked
	pthread_mutex_t  qwait_mutex;
	struct query    *query;
	char           **usernames;       // keep track of usernames and assign a user to a single thread so that they keep the same context
	int              nr_users;        // number of usernames assigned to this chat model context
	int              busy;
	int              discord_fd;      // the socket that panda.js is connected to - used to write() the tokens to
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

void event_add(int epoll_fd, int event_type, int fd)
{
	struct epoll_event event;
	event.events   = event_type;
	event.data.ptr = NULL;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}

void event_del(int epoll_fd, int event_type, int fd)
{
	struct epoll_event event;
	event.events   = event_type;
	event.data.ptr = NULL;
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event);
}

char *panda_get_token(int fd)
{
	char buf[256];
	int nbytes;

	nbytes = read(fd, buf, sizeof(buf)-1);
	if (nbytes <= 0)
		return NULL;
	buf[nbytes] = 0;
	printf("buf: %s buf[0]: %x nbytes: %d\n", buf, (unsigned char)buf[0], nbytes);
	return strdup(buf);
}

void panda_send_token(char *token, char *query_id, int discord_fd)
{
	char msg[2048];
	int  msg_size;

	msg_size = snprintf(msg, sizeof(msg)-1, "%s %s", query_id, token);
	send(discord_fd, msg, msg_size, 0);
}

void load_config() {
    FILE *config_file = fopen(CONFIG_FILE, "r");
    if (!config_file) {
        perror("Failed to open config.ini");
        exit(EXIT_FAILURE);
    }

    char line[MAX_CONFIG_LINE];
    while (fgets(line, sizeof(line), config_file)) {
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");

        if (key && value) {
            if (strcmp(key, "nr_model_instances") == 0) {
                config.nr_model_instances = atoi(value);
            } else if (strcmp(key, "panda_port") == 0) {
                config.panda_port = atoi(value);
            } else if (strcmp(key, "model") == 0) {
                config.model = strdup(value);
            }
        }
    }
    fclose(config_file);
}

void cleanup() {
	for (int x = 0; x < config.nr_model_instances; x++) {
		free(threads[x]);
	}
	free(threads);
}

int os_exec_argv(char *argv[], int epoll_fd)
{
	const char        *envp[] = { "PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", NULL };
	struct epoll_event events[2];
	char               buf[512];
	int                pipe1[2]; // the pipe between the parent and the child's STDIN
	int                pipe2[2]; // the pipe between the parent and the child's STDOUT

	// Create two pipes for standard input and output of the child process
    if (pipe(pipe1) < 0 || pipe(pipe2) < 0) {
        perror("Failed to create pipes");
        exit(EXIT_FAILURE);
    }

	/*
	 * child  reads from pipe1[0], parent writes to pipe1[1]
	 * parent reads from pipe2[0], child  writes to pipe2[1]
	 */
	switch (fork()) {
		case -1:
			exit(-1);
		case 0:
			/* Replace stdin with the read end of pipe1 and replace stdout with the write end of pipe2 */
			close(pipe1[1]);
			dup2(pipe1[0], STDIN_FILENO);
			close(pipe1[0]);
			close(pipe2[0]);
			dup2(pipe2[1], STDOUT_FILENO);
	        close(pipe2[1]);
			execve(argv[0], argv, (char **)envp);
			exit(-1);
		default:
			/* Register with epoll for events on the read end of pipe2 (the child's STDOUT) */
	        events[0].events  = EPOLLIN;
	        events[0].data.fd = pipe2[0];
	        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, 	pipe2[0], &events[0]) == -1) {
	            perror("Failed to add write end of pipe to epoll instance");
	            exit(EXIT_FAILURE);
	        }
			return pipe1[1];
	}
}

void *panda_thread(void *args)
{
	struct thread      *thread = (struct thread *)args;
	struct epoll_event  panda_events;
	struct epoll_event *events, *event;
	char                chatbuf[4096];
	char               *token;
	char               *argv[] = { "/usr/src/ai/llama.cpp/main",  "--log-disable", "-m", "/usr/src/ai/llama.cpp/openhermes-2.5-mistral-7b.Q4_K_M.gguf", "--color", "--interactive-first", NULL };
	int                 nr_events, fd, epoll_fd, chat_fd, nbytes;

	// open epoll descriptor on the parent thread
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);

	/* 
	 * fork/execve llama.cpp/main and wait for questions from panda.js which will be written to the child's stdin
	 * chat_fd is the child's standard output and epoll will be used to monitor when tokens are being written to it
	 */
	chat_fd = os_exec_argv(argv, epoll_fd);
	event_add(epoll_fd, EVENT_READ, chat_fd);

	events = (struct epoll_event *)malloc(sizeof(*events) * MAXEVENTS);	
	while (1) {
		/*
		 * Wait for new questions from panda.js (via main())
		 */
		thread->busy = 0;
		pthread_mutex_lock(&thread->qwait_mutex);
		pthread_cond_wait(&thread->qwait_condition, &thread->qwait_mutex);

		/* write the question to the llama chat program's standard input descriptor */
		write(chat_fd, thread->query->question, strlen(thread->query->question));				

		while (1) {
			nr_events = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
			for (int x = 0; x<nr_events; x++) {
				event = &events[x];
				fd    = event->data.fd;
				if (event->events & EPOLLIN) {
					/*
					 * llama.cpp is writing tokens to its stdout, read the tokens and send them to the discord bot
					 */
					token = panda_get_token(event->data.fd);
					if (!token)
						continue;
					thread->query->nr_tokens++;
					panda_send_token(token, thread->query->id, thread->discord_fd);
				} else if (event->events & EPOLLRDHUP) {
					close(event->data.fd);
					break;
				}
			}
		}
	}
}

struct query *new_query(char *question)
{
	struct query *query = malloc(sizeof(*query));
	char         *p     = strchr(question, ' ');

	if (!p)
		return NULL;
	*p++ = 0;

	query->question = strdup(p);
	query->id       = strdup(question);
	return (query);
}

int main()
{
	struct sockaddr_in  client_addr;
	struct query       *query;
	char                question_buf[4096];
	pthread_t           tid;
	int                 panda_server_fd, client_fd, nbytes, client_addr_len, nr_threads;

	load_config();
	printf("nr models: %d\n", config.nr_model_instances);
	printf("port: %d\n",      config.panda_port);
	printf("model: %s\n",     config.model);

	nr_threads = config.nr_model_instances;
	threads    = malloc(sizeof(struct thread) * nr_threads);

	for (int x = 0; x<nr_threads; x++) {
		struct thread *thread = malloc(sizeof(*thread));
		memset(thread, 0, sizeof(*thread));
		memset(&thread->qwait_condition, 0, sizeof(pthread_cond_t));
		memset(&thread->qwait_mutex, 0, sizeof(pthread_mutex_t));
		threads[x] = thread;
		pthread_create(&tid, NULL, panda_thread, thread);
	}

	panda_server_fd = net_tcp_bind(inet_addr("127.0.0.1"), config.panda_port);
	client_fd       = accept(panda_server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	while (1) {
		nbytes = read(client_fd, question_buf, sizeof(question_buf)-1);
		if (nbytes <= 0) {
			perror("Failed to read question from client");
			close(client_fd);
			exit(-1);
		}
		query = new_query(question_buf);
		if (!query)
			continue;
		printf("question: %s\n", question_buf);
		pthread_mutex_lock(&thread_lock);
		for (int x = 0; x<nr_threads; x++) {
			if (!threads[x]->busy) {
				threads[x]->busy       = 1;
				threads[x]->query      = query;
				threads[x]->discord_fd = client_fd;
				pthread_cond_signal(&threads[x]->qwait_condition);
				break;
			}
		}
		pthread_mutex_unlock(&thread_lock);
	}
}