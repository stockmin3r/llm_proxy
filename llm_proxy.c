#include <llm_proxy.h>

struct model    **models;
int               nr_models;
char             *models_json;
int               models_json_size;
struct thread   **threads;
struct config     config;
mutex_t           thread_mutex;

void *zmalloc(long size)
{
	void *ptr;

	if (size <= 0)
		return NULL;	
	ptr = (void *)malloc(size);
	if (!ptr)
		return NULL;
	memset(ptr, 0, size);
	return (ptr);
}

int cstring_line_count(char *str)
{
	char *line, *p;
	int   count = 0;

	line = str;
	while ((p=strchr(line, '\n'))) {
		count++;
		line = p+1;
	}
	return count;
}

#ifdef __LINUX__
void event_mod(int epoll_fd, int event_type, int fd)
{
	struct epoll_event event;
	event.events   = event_type;
	event.data.ptr = NULL;
	epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
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

void llm_thread_create(void *(*func)(void *), struct thread *thread)
{
	pthread_create(&thread->tid, NULL, func, thread);
	memset(&thread->qwait_condition, 0, sizeof(thread->qwait_condition));
	memset(&thread->qwait_mutex,     0, sizeof(thread->qwait_mutex));
	pthread_detach(thread->tid);
}

void thread_create(void *(*func)(void *), void *args)
{
	pthread_t tid;
	pthread_create(&tid, NULL, func, args);
	pthread_detach(tid);
}

void write_pipe(pipe_t stdin_pipe, char *question)
{
	write(stdin_pipe, question, strlen(question));
}

#define LLM_PROMPT	"POST /completion HTTP/1.1\r\n"  \
					"Connection: Keep-alive\r\n\r\n" \
					"{\"stream\":true,\"stop\":[\"</s>\",\"Llama:\",\"User:\"]},\"prompt\":\"%s\"}"
				
//{"stream":true,"n_predict":400,"temperature":0.7,"stop":["</s>","Llama:","User:"],"repeat_last_n":256,"repeat_penalty":1.18,"top_k":40,"top_p":0.95,"min_p":0.05,"tfs_z":1,"typical_p":1,"presence_penalty":0,"frequency_penalty":0,"mirostat":0,"mirostat_tau":5,"mirostat_eta":0.1,"grammar":"","n_probs":0,"image_data":[],"cache_prompt":true,"api_key":"","slot_id":-1,"prompt":"This is a conversation between User and Llama, a friendly chatbot. Llama is helpful, kind, honest, good at writing, and never fails to answer any requests immediately and with precision.\n\nUser: what is your name?\nLlama:"}
					

void llm_ask_question(struct thread *thread, char *question)
{
	if (config.use_pipes) {
		write(thread->eventloop->llm_proxy_stdin, question, strlen(question));
		return;
	}

	char  request[1024 KB];
	char *response;

	snprintf(request, sizeof(request)-1, LLM_PROMPT, question);
	if (!(response=curl_get(request)))
		return;
}

void llm_network_proxy(struct thread *thread)
{
	char              *argv[] = { "/usr/src/ai/llama.cpp/server",  "--log-disable", "-m", "/usr/src/ai/llama.cpp/openhermes-2.5-mistral-7b.Q4_K_M.gguf", "--port", NULL, NULL };
	const char        *envp[] = { "PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", NULL };
	char               portarg[8];
	pipe_t             llm_proxy_sockfd;

	// --port
	snprintf(portarg, sizeof(portarg)-1, "%d", thread->llm_port);
	argv[5] = portarg;

	switch (fork()) {
		case -1:
			exit(-1);
		case 0:
			execve(argv[0], argv, (char **)envp);
			exit(-1);
		default:
			break;
	}
	sleep(3);
	llm_proxy_sockfd  = net_tcp_connect("127.0.0.1", thread->llm_port);
	thread->eventloop = eventloop_create(llm_proxy_sockfd);
	thread->eventloop->llm_proxy_stdin = thread->eventloop->llm_proxy_stdout = llm_proxy_sockfd;
	if (llm_proxy_sockfd == -1)
		exit(-1);
}	

void llm_pipe_proxy(struct thread *thread)
{
	eventloop_t       *eventloop;
	char              *argv[]    = { "drivers/llamacpp/main",  "--log-disable", "-m", "models/openhermes-2.5-mistral-7b.Q4_K_M.gguf", "--interactive-first", NULL };
	const char        *envp[]    = { "PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin", NULL };
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
			thread->eventloop = eventloop_create(pipe2[0]);
			thread->eventloop->llm_proxy_stdin  = pipe1[1]; // child's stdin  (write questions to stdin)
			thread->eventloop->llm_proxy_stdout = pipe2[0]; // child's stdout (get answers/tokens from stdout)
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
				 * llama.cpp is writing tokens to its stdout, read the tokens and send them to the discord bot or a memory buffer
				 */
				nbytes = read(llm_proxy_stdout, token, 256);
				if (nbytes <= 0)
					continue;

				mutex_lock(&query->query_lock);
				if (query->tokens_size + nbytes >= query->max_tokens_size) {
					query->max_tokens_size *= 2;
					query->tokens = (char *)realloc(query->tokens, query->max_tokens_size);
					if (!query->tokens)
						exit(-1);
				}

				// update query->tokens with the latest 'tokens' read() from llamacpp's stdout
				memcpy(query->tokens+query->tokens_size, token, nbytes);
				query->tokens_size += nbytes;
				query->tokens[query->tokens_size] = 0;
				mutex_unlock(&query->query_lock);
				if (query->token_handler && strlen(query->tokens) > 64)
					query->token_handler(query);
				
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

uint32_t random_int()
{
	int fd, r;

	fd = open("/dev/urandom", O_RDONLY);
	read(fd, &r, 4);
	close(fd);
	return (r);
}

void random_string(char *str)
{
	static const char alphanum[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789";
	for (int i = 0; i < 7; ++i)
		str[i] = alphanum[random_int() % (sizeof(alphanum) - 1)];
	str[7] = 0;
}

socket_t net_tcp_bind(uint32_t bind_addr, unsigned short port)
{
	struct sockaddr_in serv;
	socket_t sockfd, val = 1;
	int addrlen = sizeof(serv);

	sockfd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, IPPROTO_TCP);
	if (sockfd < 0)
		return -1;

	setsockopt(sockfd, SOL_SOCKET,  SO_REUSEADDR, (const char *)&val, sizeof(val));
	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,  (const char *)&val, sizeof(val));

	memset(&serv, 0, sizeof(serv));
	serv.sin_family      = AF_INET;
	serv.sin_port        = htons(port);
	serv.sin_addr.s_addr = bind_addr;
	if (bind(sockfd, (struct sockaddr *)&serv, sizeof(serv)) == SOCKET_ERROR) {
		close(sockfd);
		return -1;
	}
	if (listen(sockfd, 15) < 0) {
		close(sockfd);
		return -1;
	}
	return (sockfd);
}

socket_t net_tcp_connect(const char *dst_addr, unsigned short dst_port)
{
	struct sockaddr_in paddr;
	int dst_fd;

	dst_fd = socket(AF_INET, SOCK_STREAM|SOCK_CLOEXEC, IPPROTO_TCP);
	if (dst_fd < 0)
		return -1;
	paddr.sin_family      = AF_INET;
	paddr.sin_port        = htons(dst_port);
	paddr.sin_addr.s_addr = inet_addr(dst_addr);
	if (connect(dst_fd, (struct sockaddr *)&paddr, sizeof(paddr)) < 0) {
		close(dst_fd);
		return -1;
	}
	return (dst_fd);
}

void net_download_model(char *modelname)
{


}

int model_present(char *modelname)
{
	struct stat sb;
	char        path[256];

	snprintf(path, sizeof(path)-1, "%s/%s", config.model_directory, modelname);
	if (stat(path, &sb) == -1)
		return 0;
	return 1;
}

void load_models()
{
	struct stat   sb;
	struct model *model;
	char          buf[1024 KB];
	char         *line, *nextline, *type, *url, *json;
	int           fd, json_size = 0, max_json_size = 4096;

	fd = open("models.csv", O_RDONLY);
	if (fd < 0)
		exit(-1);
	if (fstat(fd, &sb) == -1 || sb.st_size >= sizeof(buf))
		exit(-1);
	if (read(fd, buf, sb.st_size) <= 0)
		exit(-1);
	buf[sb.st_size] = 0;

	nr_models = cstring_line_count(buf);
	models    = (struct model **)malloc(sizeof(void *) * nr_models);
	if (!models)
		exit(-1);

	line = buf;
	json = (char *)malloc(max_json_size);
	if (!json)
		exit(-1);
	json[json_size++] = '[';

	nr_models = 0;
	while ((nextline=strchr(line, '\n'))) {
		// modelname,type,url
		if (*(nextline+1) == '\0')
			break;
		type = strchr(line, ',');
		if (!type) {
			printf("incorrect models.csv format: modelname,type,url\n");
			line = nextline + 1;
			continue;
		}
		url  = strchr(type, ',');
		if (!url) {
			printf("incorrect models.csv format: modelname,type,url\n");
			line = nextline + 1;
			continue;
		}

		*nextline++         = 0;
		*type++             = 0;

		model               = (struct model *)zmalloc(sizeof(*model));
		model->name         = strdup(line);
		model->type         = strdup(type);
		model->url          = strdup(url);
		models[nr_models++] = model;

		if (model_present(model->name))
			model->status   = "downloaded";
		else
			model->status   = "not installed";

		json_size += snprintf(json+json_size, 256, "{\"model\":\"%s\",\"type\":\"%s\",\"status\":\"%s\"},", model->name, model->type, model->status);
		if (nr_models >= MAX_MODELS)
			break;
		line = nextline;
		if (json_size + 300 >= max_json_size) {
			max_json_size *= 2;
			json = (char *)realloc(json, max_json_size);
			if (!json)
				exit(-1);
		}
	}
	json[json_size-1] = ']';
	models_json = (char *)malloc(sizeof(HTTP_JSON)+json_size+256);
	if (!models_json)
		exit(-1);
	models_json_size  = snprintf(models_json, 128, HTTP_JSON, json_size);
	memcpy(models_json+models_json_size, json, json_size);
	models_json_size += json_size;
	free(json);
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
			} else if (strcmp(key, "use_pipes") == 0) {
				config.use_pipes = atoi(value);
			} else if (strcmp(key, "llm_port_start") == 0) {
				config.llm_port_start = atoi(value);
			} else if (strcmp(key, "model_directory") == 0) {
				config.model_directory = strdup(value);
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

void discord_token_handler(struct query *query)
{
	char msg[2048];
	int  msg_size;

	msg_size = snprintf(msg, sizeof(msg)-1, "%s %s", query->id, query->tokens);
	send(query->output_fd, msg, msg_size, 0);
	query->tokens_size = 0;
	memset(query->tokens, 0, query->tokens_size);
}

void *llm_proxy_thread(void *args)
{
	struct thread  *thread = (struct thread *)args;

	/* 
	 * fork/execve llama.cpp/main and wait for questions from panda.js which will be written to the child's stdin
	 * llm_proxy_stdout is the child's standard output and select() will be used to monitor when tokens are being written to it
	 * thread->eventloop will be created by llm_pipe_proxy() or llm_network_proxy()
	 */
	if (config.use_pipes != 0)
		llm_pipe_proxy(thread);
	else
		llm_network_proxy(thread);

	while (1) {
		/*
		 * Wait for new questions from panda.js (via main())
		 */
		thread->busy = 0;
		thread_wait(thread);

		/* write the question to llamacpp's standard input */
		write_pipe(thread->eventloop->llm_proxy_stdin, thread->query->question);
		process_llm_tokens(thread, thread->eventloop->llm_proxy_stdout);
	}
}
struct query *new_query(char *question)
{
	struct query *query = (struct query *)malloc(sizeof(*query));
	char         *p     = strchr(question, ' ');

	if (!p)
		return NULL;
	*p++ = 0;

	query->question        = strdup(p);
	query->id              = strdup(question);
	query->tokens          = (char *)malloc(8192);
	query->tokens_size     = 0;
	query->max_tokens_size = 8192;
	return (query);
}

void llm_add_query(struct query *query)
{
	mutex_lock(&thread_mutex);
	for (int x = 0; x<config.nr_model_instances; x++) {
		if (!threads[x]->busy) {
			threads[x]->busy  = 1;
			threads[x]->query = query;
			switch (query->output) {
				case LLM_OUTPUT_DISCORD:
					query->token_handler = discord_token_handler;
					break;
			}
			thread_signal(threads[x]);
			break;
		}
	}
	mutex_unlock(&thread_mutex);
}


int main()
{
	struct sockaddr_in  client_addr;
	struct thread      *thread;
	struct query       *query;
	char                question_buf[4096];
	int                 panda_server_fd, client_fd, nbytes, client_addr_len, nr_threads;

	fs_mkdir("models", 0644);
	load_models();
	init_os();
	init_http();
	load_config();
	printf("port: %d\n",      config.panda_port);
	printf("model: %s\n",     config.model);

	nr_threads = config.nr_model_instances;
	threads    = (struct thread **)malloc(sizeof(struct thread) * nr_threads);
	for (int x = 0; x<nr_threads; x++) {
		thread           = (struct thread *)zmalloc(sizeof(*thread));
		threads[x]       = thread;
		thread->llm_port = config.llm_port_start++;
		llm_thread_create(llm_proxy_thread, thread);
	}

	panda_server_fd = net_tcp_bind(inet_addr("127.0.0.1"), config.panda_port);
	while (1) {
		client_fd = accept(panda_server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd == -1 && errno == EINTR)
			continue;
		while (1) {
			nbytes = recv(client_fd, question_buf, sizeof(question_buf)-1, 0);
			if (nbytes <= 0)
				break;
			query = new_query(question_buf);
			if (!query)
				continue;

			/*
			 * Add query to a thread that manages an LLM which isn't busy
			 */
			query->output_fd = client_fd;
			query->output    = LLM_OUTPUT_DISCORD;
			llm_add_query(query);
		}
		close(client_fd);
	}
}
