#ifndef __LLM_PROXY
#define __LLM_PROXY


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include <os.h>

/* C++ */
#include <string>
#include <memory>
#include <map>
#include <functional>

#ifdef __LINUX__
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "/usr/include/x86_64-linux-gnu/curl/curl.h"
#define __LIBCURL__ 1
#endif

#ifdef __WINDOWS__
#include <synchapi.h>
#include <sdkddkver.h>
#include <winsock2.h>
#include <winioctl.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#endif

#define HTTP_GET   "HTTP/1.1 200 OK\r\n"                 \
                   "Connection: close\r\n"               \
                   "Content-Type: text/html\r\n"         \
                   "Content-Length: %d\r\n\r\n"          \


#define HTTP_IMAGE "HTTP/1.1 200 OK\r\n"                 \
                   "Content-Type: image/png\r\n"         \
                   "Connection: close\r\n"               \
                   "Content-Length: %d\r\n\r\n"          \

#define HTTP_JSON "HTTP/1.1 200 OK\r\n"                  \
                  "Content-type: application/json\r\n"   \
				  "Connection: close\r\n"                \
                  "Content-Length: %d\r\n\r\n"           \

#define HTTP_TXT  "HTTP/1.1 200 OK\r\n"                  \
                  "Content-type: text/html\r\n"          \
				  "Connection: close\r\n"                \
                  "Content-Length: %d\r\n\r\n%s"         \

#define HTTP_CHUNK "HTTP/1.1 200 OK\r\n"                 \
                   "Content-type: text/html\r\n"         \
                   "Connection: keep-alive\r\n"          \
                   "Transfer-Encoding: chunked\r\n"      \
                   "Content-Length: %d\r\n\r\n%s"        \

#define KB * 1024

#define CONFIG_FILE           "config.ini"
#define MAX_CONFIG_LINE       1024
#define MAXEVENTS             512
#define PANDA_PORT            8484
#define LOCALHOST             0x0100007F
#define LLM_OUTPUT_DISCORD    1

struct query;
struct eventloop;

typedef int                   event_fd_t;
typedef unsigned int          uint32_t;
typedef struct eventloop      eventloop_t;

typedef void (*token_handler_f)(struct query *query);

#ifdef __LINUX__
#define EVENT_READ            EPOLLIN
#define EVENT_WRITE           EPOLLOUT
#endif

#ifdef __LINUX__
typedef int                   socket_t;
typedef int                   pipe_t;
typedef pthread_mutex_t       mutex_t;
typedef pthread_cond_t        condition_t;
typedef pthread_t             tid_t;
typedef unsigned long         uint64_t;
typedef int                   SOCKET;
#define mutex_lock(mtx)       pthread_mutex_lock  (mtx)
#define mutex_unlock(mtx)     pthread_mutex_unlock(mtx)
#define fs_mkdir(name,access) mkdir((const char *)name, access)

struct eventloop {
	struct epoll_event *events;
	int                 epoll_fd;
	pipe_t              llm_proxy_stdin;
	pipe_t              llm_proxy_stdout;
};

struct filemap {
	char               *map;      /* memory address */
	uint64_t            filesize; /* filesize */
	int                 fd;       /* fd */
};

#define SOCKET_ERROR -1

#define mutex_create(mtx) memset(mtx, 0, sizeof(mtx))

#endif

#ifdef __WINDOWS__
typedef SOCKET                socket_t;
typedef HANDLE                pipe_t;
typedef HANDLE                tid_t;
#define SOCK_CLOEXEC          0
#define condition_t           CONDITION_VARIABLE
#define mutex_t               CRITICAL_SECTION
#define in_addr_t             struct in_addr
#define fs_mkdir(name,access) CreateDirectory((const char *)name, NULL)
typedef unsigned long long    uint64_t;

struct eventloop {
	HANDLE       iocp;
	pipe_t       llm_proxy_stdin;
	pipe_t       llm_proxy_stdout;
};

struct filemap {
	HANDLE       hMap;     /* Windows CreateFileMapping Handle */
	HANDLE       hFile;    /* Windows CreateFile Handle        */
	uint64_t     filesize; /* filesize */
	int          fd;       /* osh fd */
};


void     mutex_lock        (mutex_t *mtx);
void     mutex_unlock      (mutex_t *mtx);
void     mutex_create      (mutex_t *mtx);
void     printError        (void);

typedef struct _OVERLAPPED_EX {
    OVERLAPPED Overlapped;
    SOCKET     ClientSocket;
    WSABUF     Buffer;
    SIZE_T     BufferLength;
    SIZE_T     BytesSent;
    SIZE_T     TotalBytesToSend;
    ULONG      BytesTransferred;
} OVERLAPPED_EX, * LPOVERLAPPED_EX;

#endif

#define MAX_MODELS 1024

struct model {
	char          *name;                  // GGUF filename
	char          *type;                  // code | medical | law
	char          *org;                   // creator of model
	const char    *status;                // downloaded ?
	char          *url;                   // huggingface URL of model
	int            nr_votes;
};

struct query {
	char            *question;
	char            *id;              // question ID (used by discord.js to lookup the message object)
	char            *tokens;          // tokens generated by the LLM
	int              tokens_size;
	int              max_tokens_size;
	int              output_fd;       // the socket that panda.js is connected to - used to write() the tokens to
	int              output;          // socket output or -1 for only storing the tokens into a buffer
	mutex_t          query_lock;      // lock for synchronizing access to the query->tokens buffer
	token_handler_f  token_handler;   // function that sends tokens to either discord or an internal buffer (for WWW)
};

typedef struct query query_t;

/**
 * @brief Represents a thread in the LLM proxy.
 * 
 * This struct holds information about a thread in the LLM proxy, including the condition and mutex for waiting, 
 * the eventloop for managing the stdout descriptor, the thread ID, the associated model and driver, the user's query, 
 * the usernames assigned to this thread, the number of assigned usernames, the busy status, and the llamacpp server port.
 */
struct thread {
	condition_t      qwait_condition; // thread will wait until a question is asked
	mutex_t          qwait_mutex;     // mutex for the wait condition (CRITICAL_SECTION on windows, pthread_cond_t on linux)
	eventloop_t     *eventloop;       // eventloop to manage the stdout descriptor of this thread's llama.cpp instance
	tid_t            tid;             // thread id
	char            *model;           // the model associated with this LLM instance
	char            *driver;          // llamacpp
	struct query    *query;           // user's question
	char           **usernames;       // keep track of usernames and assign a user to a single thread so that they keep the same context
	int              nr_users;        // number of usernames assigned to this chat model context
	int              busy;            // currently generating tokens for a specific question
	unsigned short   llm_port;        // llamacpp server port
};

struct config {
	char          *model;               // model filename
	char          *model_directory;     // directory containing model files
	char          *url;                 // where to find the model online
	unsigned short panda_port;          // localhost port for the discord bot to connect to
	unsigned short llm_port_start;      // llamacpp server --port (llm_port_start +1 for each instance)
	int            nr_model_instances;  // number of models to run
	int            nr_gpu_layers;       // --n-gpu-layers (llamacpp)
	int            ctx_size;            // --ctx-size     (llamacpp)
	int            use_pipes;           // use pipes or connect to llamacpp http server
	int            timeout;             // maximum amount of time to generate tokens for one question
	double         top_p;               // --top_p        (llamacpp)
};


extern char *models_json;
extern int   models_json_size;

void           discord_token_handler (struct query *query);
char          *curl_get              (char *url);
eventloop_t   *eventloop_create      (pipe_t stdout_pipe);
void           llm_add_query         (struct query *query);

extern struct config   config;
extern struct thread **threads;
extern mutex_t         thread_mutex;

socket_t net_tcp_bind      (uint32_t bind_addr, unsigned short port);
socket_t net_tcp_connect   (const char *dst_addr, unsigned short dst_port);
void     random_string     (char *str);
int      cstring_line_count(char *str);
void     init_http         (void);
void     load_resources    (void);
void     thread_create     (void *(*func)(void *), void *args);
void    *zmalloc           (long size);
void     init_os           (void);
void     llm_thread_create (void *(*func)(void *), struct thread *thread);
void     llm_pipe_proxy    (struct thread *thread);
void     llm_network_proxy (struct thread *thread);
void     process_llm_tokens(struct thread *thread, pipe_t llm_proxy_stdout);
void     write_pipe        (pipe_t stdin_pipe, char *question);
void     thread_signal     (struct thread *thread);
void     thread_wait       (struct thread *thread);
char    *fs_mapfile_rw(char *path, struct filemap *filemap);

void cstring_strstr_replace(char *, char *);

char *curl_get(char *url);


#endif
