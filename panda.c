#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
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

#define CONFIG_FILE           "config.ini"
#define MAX_CONFIG_LINE       1024
#define MAXEVENTS             512
#define PANDA_PORT            8484
#define LOCALHOST             0x0100007F

#ifdef __LINUX__
#define EVENT_READ            EPOLLIN
#define EVENT_WRITE           EPOLLOUT
#endif

#ifdef __LINUX__
typedef int                   socket_t;
typedef int                   pipe_t;
typedef pthread_mutex_t       mutex_t;
typedef pthread_cond_t        condition_t;
#define mutex_lock(mtx)       pthread_mutex_lock  (mtx)
#define mutex_unlock(mtx)     pthread_mutex_unlock(mtx)

struct eventloop {
	struct epoll_event *events;
	int                 epoll_fd;
};

#endif

#ifdef __WINDOWS__
typedef SOCKET                socket_t;
typedef HANDLE                pipe_t;
#define SOCK_CLOEXEC          0
#define condition_t           HANDLE
#define mutex_t               CRITICAL_SECTION
#define in_addr_t             struct in_addr

struct eventloop {
	HANDLE       iocp;
};

#endif

void  panda_send_token(char *token, char *query_id, int discord_fd);

typedef int                   event_fd_t;
typedef unsigned int          uint32_t;
typedef struct eventloop      eventloop_t;

char *index_html;
int   index_html_size;

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
	char     *tokens;
	int       tokens_size;
	int       max_tokens_size;
};

typedef struct query query_t;

struct thread {
	condition_t      qwait_condition; // thread will wait until a question is asked
	mutex_t          qwait_mutex;     // mutex for the wait condition (CRITICAL_SECTION on windows, pthread_cond_t on linux)
	eventloop_t     *eventloop;
	char            *model;           // the model associated with this LLM instance
	char            *driver;          // llamacpp|alpaca
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

void *zmalloc(long size)
{
	void *ptr;

	if (size <= 0)
		return NULL;	
	ptr = malloc(size);
	if (!ptr)
		return NULL;
	memset(ptr, 0, size);
	return (ptr);
}

#ifdef __WINDOWS__
/* threads/synchronization */
void thread_create(void *(*thread)(void *), void *args)
{
	void *TID;
	TID = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)thread, args, 0, NULL);
}

void mutex_lock(mutex_t *mtx)
{
	EnterCriticalSection(mtx);
}

void mutex_unlock(mutex_t *mtx)
{
	LeaveCriticalSection(mtx);
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

void thread_signal(struct thread *thread)
{
	EnterCriticalSection(&thread->qwait_mutex);
	SetEvent(thread->qwait_condition);
	LeaveCriticalSection(&thread->qwait_mutex);
}

void write_pipe(pipe_t stdin_pipe, char *question)
{
	DWORD dwWritten;

	WriteFile(stdin_pipe, question, strlen(question), &dwWritten, NULL);
}

void llm_proxy(pipe_t *child_stdin, pipe_t *child_stdout)
{
    STARTUPINFO         start_info = { sizeof(start_info) };
    PROCESS_INFORMATION process_info;
	SECURITY_ATTRIBUTES saAttr;
	pipe_t              stdin_READ  = NULL,  stdin_WRITE = NULL;
	pipe_t              stdout_READ = NULL, stdout_WRITE = NULL;
	char                cmdLine[256];

	saAttr.nLength              = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle       = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 

	// Create a pipe for the child process's STDIN
	if (!CreatePipe(&stdin_READ, &stdin_WRITE, &saAttr, 0))
		exit(-1);

	// Create a pipe for the child process's STDOUT
	if (!CreatePipe(&stdout_READ, &stdout_WRITE, &saAttr, 0))
		exit(-1);

	// Ensure the write handle to the pipe for STDIN is not inherited.  
	if (!SetHandleInformation(stdin_WRITE, HANDLE_FLAG_INHERIT, 0))
		exit(-1);

	// Ensure the read handle to the pipe for STDOUT is not inherited
	if (!SetHandleInformation(stdout_READ, HANDLE_FLAG_INHERIT, 0))
		exit(-1);

	// Create a STARTUPINFO struct and set the standard output to stdout_WRITE and stdin to stdin_READ
	ZeroMemory(&start_info, sizeof(STARTUPINFO));
	start_info.cb         = sizeof(STARTUPINFO); 
	start_info.hStdError  = stdout_WRITE;
	start_info.hStdOutput = stdout_WRITE;
	start_info.hStdInput  = stdin_READ;
	start_info.dwFlags   |= STARTF_USESTDHANDLES;

	// keep track of main.exe's STDIN/STDOUT
	*child_stdin  = stdout_READ;
	*child_stdout = stdin_WRITE;

    // Create a new process that reads from the pipe and writes to the standard output
	snprintf(cmdLine, sizeof(cmdLine)-1, "main.exe --interactive-first -m %s", config.model);
	if (!CreateProcessA("c:\\ai\\llamacpp\\main.exe", cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &start_info, &process_info))
		exit(-1);

	CloseHandle(stdout_WRITE);
	CloseHandle(stdin_READ);
}

eventloop_t *eventloop_create(pipe_t stdin_pipe)
{
	eventloop_t *eventloop = malloc(sizeof(*eventloop));
	memset(eventloop, 0, sizeof(*eventloop));
	return (eventloop);
}

void process_llm_tokens(struct thread *thread, pipe_t llm_proxy_stdout)
{
	struct query *query = thread->query;
	char          token[8192];
	int           size, bytesRead;

	while (PeekNamedPipe(llm_proxy_stdout, NULL, NULL, NULL, &size, NULL)) {
		if (size == 0) {
			Sleep(500);
			continue;
		}
		ReadFile(llm_proxy_stdout, token, size, &bytesRead, NULL);		
		printf("token: %s\n", token);
		if (!token)
			continue;
		if (query->tokens_size + bytesRead >= query->max_tokens_size) {
			query->max_tokens_size *= 2;
			query->tokens = realloc(query->tokens, query->max_tokens_size);
			if (!query->tokens)
				exit(-1);
		}
		memcpy(query->tokens+query->tokens_size, token, bytesRead);
		query->tokens[query->tokens_size] = 0;
		if (strlen(query->tokens) >= 64) {
			panda_send_token(query->tokens, query->id, thread->discord_fd);
			query->tokens_size = 0;
			memset(query->tokens, 0, query->tokens_size);
		}
	}
}

void init_os()
{
	WSADATA wsaData;
	WORD    versionRequested = MAKEWORD(2, 2);
	WSAStartup(versionRequested, &wsaData);
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

/* Wake up thread driving llamacpp's main.exe */
void thread_signal(struct thread *thread)
{
	pthread_cond_signal(&thread->qwait_condition);
}

void thread_create(void *(*thread)(void *), void *args)
{
	pthread_t tid;
	pthread_create(&tid, NULL, thread, args);
	pthread_detach(tid);
}

void write_pipe(pipe_t stdin_pipe, char *question)
{
	write(stdin_pipe, question, strlen(question));
}

void llm_proxy(pipe_t *child_stdin, pipe_t *child_stdout)
{
	char              *argv[] = { "/usr/src/ai/llama.cpp/main",  "--log-disable", "-m", "/usr/src/ai/llama.cpp/openhermes-2.5-mistral-7b.Q4_K_M.gguf", "--interactive-first", NULL };
	const char        *envp[] = { "PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", NULL };
	char               buf[512];
	int                pipe1[2]; // the pipe between the parent and the child's STDIN
	int                pipe2[2]; // the pipe between the parent and the child's STDOUT

	// Create two pipes for standard input and output of the child process
	if (pipe(pipe1) < 0 || pipe(pipe2) < 0)
		exit(-1);

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

void process_llm_tokens(struct thread *thread, pipe_t llm_proxy_stdout)
{
	eventloop_t        *eventloop = thread->eventloop;
	query_t            *query     = thread->query;
	struct epoll_event *event;
	char                token[8192];
	int                 nr_events, nbytes, fd;

	while (1) {
		nr_events = epoll_wait(thread->eventloop->epoll_fd, thread->eventloop->events, MAXEVENTS, -1);
		for (int x = 0; x<nr_events; x++) {
			event = &eventloop->events[x];
			if (event->events & EPOLLIN) {
				/*
				 * llama.cpp is writing tokens to its stdout, read the tokens and send them to the discord bot
				 */
				nbytes = read(llm_proxy_stdout, token, 256);
				if (nbytes <= 0)
					continue;
				token[nbytes] = 0;

				if (query->tokens_size + nbytes >= query->max_tokens_size) {
					query->max_tokens_size *= 2;
					query->tokens = realloc(query->tokens, query->max_tokens_size);
					if (!query->tokens)
						exit(-1);
				}
				memcpy(query->tokens+query->tokens_size, token, nbytes);
				query->tokens_size += nbytes;
				query->tokens[query->tokens_size] = 0;
				if (strlen(query->tokens) > 64) {
					panda_send_token(query->tokens, thread->query->id, thread->discord_fd);
					query->tokens_size = 0;
				}
			} else if (event->events & EPOLLRDHUP) {
				close(event->data.fd);
				break;
			}
		}
	}
}

eventloop_t *eventloop_create(pipe_t stdout_pipe)
{
	struct epoll_event *events;
	eventloop_t        *eventloop = (eventloop_t *)zmalloc(sizeof(*eventloop));

	eventloop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	eventloop->events   = (struct epoll_event *)malloc(sizeof(*events) * MAXEVENTS);
	event_add(eventloop->epoll_fd, EVENT_READ, stdout_pipe);
	return (eventloop);	
}

void init_os()
{

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

void http_ask_question(int fd, char *request)
{


}

void http_set_model(int fd, char *request)
{


}

void *http_server(void *args)
{
	struct sockaddr_in srv, cli;
	char               buf[1024];
	socklen_t          slen   = 16;
	int                val    = 1, sockfd, client_fd;
	unsigned short     port   = 8086;

	sockfd = net_tcp_bind(LOCALHOST, port);
	if (sockfd == -1)
		exit(-1);
	for (;;) {
		client_fd = accept(sockfd, (struct sockaddr *)&cli, &slen);
		if (client_fd < 0)
			continue;
		recv(client_fd, buf, 1024, 0);
		if (strstr(buf, "/question/"))
			http_ask_question(client_fd, buf);
		else if (strstr(buf, "/model/"))
			http_set_model(client_fd, buf);
		else if (strstr(buf, "GET /index.html HTTP/1.1"))
			send(client_fd, index_html, index_html_size, 0);
		close(client_fd);
	}
}

void load_html() {
	struct stat sb;
	int fd, nbytes;

	fd = open("index.html", O_RDONLY);
	if (fd <= 0)
		exit(-1);
	fstat(fd, &sb);
	index_html_size = sb.st_size;
	index_html      = malloc(index_html_size);
	nbytes          = read(fd, index_html, index_html_size);
	if (nbytes != index_html_size)
		exit(-1);
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

void panda_send_token(char *token, char *query_id, int discord_fd)
{
	char msg[2048];
	int  msg_size;

	msg_size = snprintf(msg, sizeof(msg)-1, "%s %s", query_id, token);
	send(discord_fd, msg, msg_size, 0);
}

void *llm_proxy_thread(void *args)
{
	struct thread      *thread = (struct thread *)args;
	struct timeval      timeout;
	fd_set              rdset;
	char                chatbuf[4096];
	char                tokenbuf[1024];
	char               *token;
	pipe_t              llm_proxy_stdin, llm_proxy_stdout;
	int                 nr_events, nready, fd, nbytes;

	/* 
	 * fork/execve llama.cpp/main and wait for questions from panda.js which will be written to the child's stdin
	 * llm_proxy_stdout is the child's standard output and select() will be used to monitor when tokens are being written to it
	 */
	llm_proxy(&llm_proxy_stdin, &llm_proxy_stdout);
	thread->eventloop = eventloop_create(llm_proxy_stdout);
	while (1) {
		/*
		 * Wait for new questions from panda.js (via main())
		 */
		thread->busy = 0;
		thread_wait(thread);

		/* write the question to the llama chat program's standard input */
		write_pipe(llm_proxy_stdin, thread->query->question);
		process_llm_tokens(thread, llm_proxy_stdout);
	}
}

struct query *new_query(char *question)
{
	struct query *query = malloc(sizeof(*query));
	char         *p     = strchr(question, ' ');

	if (!p)
		return NULL;
	*p++ = 0;

	query->question        = strdup(p);
	query->id              = strdup(question);
	query->tokens          = malloc(8192);
	query->tokens_size     = 0;
	query->max_tokens_size = 8192;
	return (query);
}

int main()
{
	struct sockaddr_in  client_addr;
	struct query       *query;
	char                question_buf[4096];
	int                 panda_server_fd, client_fd, nbytes, client_addr_len, nr_threads;

	init_os();
	load_config();
	load_html();
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
		thread_create(llm_proxy_thread, thread);
	}

	panda_server_fd = net_tcp_bind(inet_addr("127.0.0.1"), config.panda_port);
	client_fd       = accept(panda_server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	while (1) {
		nbytes = recv(client_fd, question_buf, sizeof(question_buf)-1, 0);
		if (nbytes <= 0) {
			perror("Failed to read question from client");
			close(client_fd);
			exit(-1);
		}
		query = new_query(question_buf);
		if (!query)
			continue;
		mutex_lock(&thread_lock);
		for (int x = 0; x<nr_threads; x++) {
			if (!threads[x]->busy) {
				threads[x]->busy       = 1;
				threads[x]->query      = query;
				threads[x]->discord_fd = client_fd;
				thread_signal(threads[x]);
				break;
			}
		}
		mutex_unlock(&thread_lock);
	}
}
