#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include "include/os.h"

#ifdef __LINUX__
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#endif

#ifdef __WINDOWS__
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#define CONFIG_FILE     "config.ini"
#define MAX_CONFIG_LINE 1024

#define MAXEVENTS       512
#define PANDA_PORT      8484

#ifdef __LINUX__
#define PEVENT_READ      EPOLLIN
#define PEVENT_WRITE     EPOLLOUT
#endif


#ifdef __LINUX__
typedef int socket_t;
#endif

#ifdef __WINDOWS__
typedef SOCKET socket_t;
#define SOCK_CLOEXEC 0
#define mutex_t HANDLE
#endif

typedef int                event_fd_t;
typedef unsigned int       uint32_t;

struct config {
	char          *model;               // model filename
	int            nr_model_instances;  // number of models to run
	unsigned short panda_port;          // localhost port for the discord bot to connect to
	int            timeout;
};

/* Panda Event */
struct pevent {
	int       type;                  // PEVENT_READ|PEVENT_WRITE
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
mutex_t                thread_lock;

struct event {
	event_fd_t    event_fd;
};

struct connection {
	struct event  event;
	int           fd;
	unsigned int  events;
};

#ifdef __WINDOWS__
/* threads/synchronization */
void thread_create(void *(*thread)(void *), void *args)
{
        void *TID;
        TID = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread, args, 0, NULL);
}
void mutex_lock(mutex_t mtx)
{
        WaitForSingleObject(mtx, INFINITE);
}
void mutex_unlock(mutex_t mtx)
{
        ReleaseMutex(mtx);
}

/* networking */
void net_socket_block(socket_t sockfd)
{
	DWORD nb = 1;
	ioctlsocket(sockfd, FIONBIO, &nb);
}
void net_socket_nonblock(socket_t sockfd)
{
	DWORD nb = 0;
	ioctlsocket(sockfd, FIONBIO, &nb);
}

void thread_wait(struct thread *thread)
{
	WaitForSingleObject(&thread->qwait_condition, INFINITE);
}

void os_exec(char *argv[], int *child_stdin, int *child_stdout)
{
    STARTUPINFO         start_info = { sizeof(start_info) };
    PROCESS_INFORMATION process_info;
    HANDLE              read_pipe[2];

    if (!CreatePipe(read_pipe, NULL, NULL, 0))
        return;

    // Create a STARTUPINFO struct and set the standard output to the write end of the pipe
    start_info.dwFlags   |= STARTF_USESTDHANDLES;
    start_info.hStdOutput = read_pipe[1];

    // Create a new process that reads from the pipe and writes to the standard output
    if (!CreateProcess(NULL, "c:\\ai\\llamacpp\\main.exe", NULL, NULL, TRUE, 0, NULL, NULL, &start_info, &process_info)) {
        CloseHandle(read_pipe[0]);
        return;
    }

    // Read from the pipe and print the output
    char  buffer[128];
    DWORD bytes_read;
    while (TRUE) {
        if (!ReadFile(read_pipe[0], buffer, sizeof(buffer), &bytes_read, NULL)) {
            break;
        }
    }

    // Close the pipe and wait for the child process to exit
    CloseHandle(read_pipe[0]);
    WaitForSingleObject(process_info.hProcess, INFINITE);
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
}

#endif

#ifdef __LINUX__
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

void net_socket_nonblock(socket_t sockfd)
{
	int val = fcntl(sockfd, F_GETFL);
	fcntl(sockfd, F_SETFL, val|O_NONBLOCK);
}

void thread_wait(struct thread *thread)
{
	mutex_lock(&thread->qwait_mutex);
	pthread_cond_wait(&thread->qwait_condition, &thread->qwait_mutex);
}

void os_exec(char *argv[], int *child_stdin, int *child_stdout)
{
	const char        *envp[] = { "PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", NULL };
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
			*child_stdin  = pipe1[1];  // child's stdin  (write questions to stdin)
			*child_stdout = pipe2[0];  // child's stdout (get answers/tokens from stdout)
			break;
	}
}
#endif

socket_t net_tcp_bind(uint32_t bind_addr, unsigned short port)
{
	struct sockaddr_in serv;
	int sockfd, val = 1;

	sockfd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, IPPROTO_TCP);
	if (sockfd < 0)
		return -1;

	setsockopt(sockfd, SOL_SOCKET,  SO_REUSEADDR, (const char *)&val, sizeof(val));
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
            } else if (strcmp(key, "timeout") == 0) {
				config.timeout = atoi(value);
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

/*************************************
 *           TOKENS
 ************************************/
char *panda_get_token(int fd, char *tokenbuf)
{
	int nbytes = read(fd, tokenbuf, 256);
	if (nbytes <= 0)
		return NULL;
	tokenbuf[nbytes] = 0;
	return (tokenbuf);
}

void panda_send_token(char *token, char *query_id, int discord_fd)
{
	char msg[2048];
	int  msg_size;

	msg_size = snprintf(msg, sizeof(msg)-1, "%s %s", query_id, token);
	send(discord_fd, msg, msg_size, 0);
	printf("%s\n", msg);
}

void *panda_thread(void *args)
{
	struct thread      *thread = (struct thread *)args;
	struct timeval      timeout;
	fd_set              rdset;
	char                chatbuf[4096];
	char                tokenbuf[1024];
	char               *token;
	char               *argv[] = { "/usr/src/ai/llama.cpp/main",  "--log-disable", "-m", "/usr/src/ai/llama.cpp/openhermes-2.5-mistral-7b.Q4_K_M.gguf", "--color", "--interactive-first", NULL };
	int                 nr_events, nready, fd, epoll_fd, chat_fd, token_fd, nbytes;

	/* 
	 * fork/execve llama.cpp/main and wait for questions from panda.js which will be written to the child's stdin
	 * chat_fd is the child's standard output and epoll will be used to monitor when tokens are being written to it
	 */
	os_exec(argv, &chat_fd, &token_fd);
	while (1) {
		/*
		 * Wait for new questions from panda.js (via main())
		 */
		thread->busy = 0;
		thread_wait(thread);

		/* write the question to the llama chat program's standard input descriptor */
		write(chat_fd, thread->query->question, strlen(thread->query->question));				
		printf("chatfd: %d tokenfd: %d\n", chat_fd, token_fd);
		while (1) {
			FD_ZERO(&rdset);
			FD_SET(token_fd, &rdset);
			timeout.tv_sec  = config.timeout;
			timeout.tv_usec = 0;
			nready = select(token_fd+1, &rdset, NULL, NULL, &timeout);
			printf("select nready: %d\n", nready);
			if (nready == -1 && errno == EINTR) {
				continue;
			} else if (nready == 0) {
				/* Timeout */
				// loop through all threads to see if the timeout has expired so a CTRL+C can be sent to suspend the token generation
			} else if (FD_ISSET(token_fd, &rdset)) {
				/*
				 * llama.cpp is writing tokens to its stdout, read the tokens and send them to the discord bot
				 */
				token = panda_get_token(token_fd, tokenbuf);
				if (!token)
					continue;
				thread->query->nr_tokens++;
				panda_send_token(token, thread->query->id, thread->discord_fd);
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
		mutex_lock(&thread_lock);
		for (int x = 0; x<nr_threads; x++) {
			if (!threads[x]->busy) {
				threads[x]->busy       = 1;
				threads[x]->query      = query;
				threads[x]->discord_fd = client_fd;
				pthread_cond_signal(&threads[x]->qwait_condition);
				break;
			}
		}
		mutex_unlock(&thread_lock);
	}
}
