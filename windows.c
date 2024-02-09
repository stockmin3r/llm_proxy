/**
 * @file windows.c
 * @brief Windows-specific implementation of functions in llm_proxy.h
 */

#include <llm_proxy.h>

#ifdef __WINDOWS__

/**
 * @brief Create a new thread and execute the specified function.
 * 
 * @param func Pointer to the function to be executed by the thread.
 * @param thread Pointer to the thread structure.
 */
void llm_thread_create(void *(*func)(void *), struct thread *thread)
{
	// Implementation details...
}

/**
 * @brief Create a new thread and execute the specified function.
 * 
 * @param func Pointer to the function to be executed by the thread.
 * @param args Pointer to the arguments to be passed to the function.
 */
void thread_create(void *(*func)(void *), void *args)
{
	// Implementation details...
}

/**
 * @brief Wait for a thread to finish execution.
 * 
 * @param thread Pointer to the thread structure.
 */
void thread_wait(struct thread *thread)
{
	// Implementation details...
}

/**
 * @brief Signal a thread to resume execution.
 * 
 * @param thread Pointer to the thread structure.
 */
void thread_signal(struct thread *thread)
{
	// Implementation details...
}

/**
 * @brief Lock a mutex.
 * 
 * @param mtx Pointer to the mutex.
 */
void mutex_lock(mutex_t *mtx)
{
	// Implementation details...
}

/**
 * @brief Unlock a mutex.
 * 
 * @param mtx Pointer to the mutex.
 */
void mutex_unlock(mutex_t *mtx)
{
	// Implementation details...
}

/**
 * @brief Create a new mutex.
 * 
 * @param mtx Pointer to the mutex.
 */
void mutex_create(mutex_t *mtx)
{
	// Implementation details...
}

/**
 * @brief Process LLM tokens received from a pipe.
 * 
 * @param thread Pointer to the thread structure.
 * @param llm_proxy_stdout Pipe for receiving LLM tokens.
 */
void process_llm_tokens(struct thread *thread, pipe_t llm_proxy_stdout)
{
	// Implementation details...
}

/**
 * @brief Set a socket to blocking mode.
 * 
 * @param sockfd Socket file descriptor.
 */
void net_socket_block(socket_t sockfd)
{
	// Implementation details...
}

/**
 * @brief Set a socket to non-blocking mode.
 * 
 * @param sockfd Socket file descriptor.
 */
void net_socket_nonblock(socket_t sockfd)
{
	// Implementation details...
}

/**
 * @brief Print the last error message.
 */
void printError()
{
	// Implementation details...
}

/**
 * @brief Main function for the LLM network proxy.
 * 
 * @param thread Pointer to the thread structure.
 */
void llm_network_proxy(struct thread *thread)
{
	// Implementation details...
}

/**
 * @brief Write data to a pipe.
 * 
 * @param stdin_pipe Pipe for writing data.
 * @param question Data to be written.
 */
void write_pipe(pipe_t stdin_pipe, char *question)
{
	// Implementation details...
}

/**
 * @brief Main function for the LLM pipe proxy.
 * 
 * @param thread Pointer to the thread structure.
 */
void llm_pipe_proxy(struct thread *thread)
{
	// Implementation details...
}

/**
 * @brief Create an event loop.
 * 
 * @param stdin_pipe Pipe for reading data.
 * @return Pointer to the created event loop.
 */
eventloop_t *eventloop_create(pipe_t stdin_pipe)
{
	// Implementation details...
}

/**
 * @brief Initialize the operating system.
 */
void init_os(void)
{
	// Implementation details...
}

#endif
#include <llm_proxy.h>

#ifdef __WINDOWS__
/* threads/synchronization */
void llm_thread_create(void *(*func)(void *), struct thread *thread)
{
	InitializeConditionVariable(&thread->qwait_condition);
	InitializeCriticalSection(&thread->qwait_mutex);
	thread->tid = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, thread, 0, NULL);
}

void thread_create(void *(*func)(void *), void *args)
{
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, args, 0, NULL);
}

void thread_wait(struct thread *thread)
{
	EnterCriticalSection(&thread->qwait_mutex);
	SleepConditionVariableCS(&thread->qwait_condition, &thread->qwait_mutex, INFINITE);
}

void thread_signal(struct thread *thread)
{
	LeaveCriticalSection(&thread->qwait_mutex);
	WakeConditionVariable(&thread->qwait_condition);
}

void mutex_lock(mutex_t *mtx)
{
	EnterCriticalSection(mtx);
}

void mutex_unlock(mutex_t *mtx)
{
	LeaveCriticalSection(mtx);
}

void mutex_create(mutex_t *mtx)
{
	InitializeCriticalSection(mtx);
}

void process_llm_tokens(struct thread *thread, pipe_t llm_proxy_stdout)
{
	struct query *query = thread->query;
	char          token[8192];
	DWORD         size, bytesRead;

	while (1) {
		ReadFile(llm_proxy_stdout, token, sizeof(token)-1, &bytesRead, NULL);
		if (!bytesRead)
			return;

		mutex_lock(&query->query_lock);
		if (query->tokens_size + bytesRead >= query->max_tokens_size) {
			query->max_tokens_size *= 2;
			query->tokens = (char *)realloc(query->tokens, query->max_tokens_size);
			if (!query->tokens)
				exit(-1);
		}
	
			memcpy(query->tokens+query->tokens_size, token, bytesRead);
		query->tokens_size += bytesRead;
		query->tokens[query->tokens_size] = 0;
		mutex_unlock(&query->query_lock);
	}
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

void printError()
{
	DWORD lastError = GetLastError();
	LPSTR errorMessage;
	DWORD requiredBufferSize = FormatMessage(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL, lastError, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPSTR)&errorMessage, 0, NULL);
	errorMessage = (LPSTR) malloc(requiredBufferSize+100);
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lastError, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (LPSTR)&errorMessage, 0, NULL);
//	free(errorMessage);
	printf("%s\n", errorMessage);
}


void llm_network_proxy(struct thread *thread)
{

}

void write_pipe(pipe_t stdin_pipe, char *question)
{
	DWORD dwWritten;

	WriteFile(stdin_pipe, question, strlen(question), &dwWritten, NULL);
}

void llm_pipe_proxy(struct thread *thread)
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
	thread->eventloop     = eventloop_create(stdout_READ);
	thread->eventloop->llm_proxy_stdin  = stdin_WRITE;
	thread->eventloop->llm_proxy_stdout = stdout_READ;

    // Create a new process that reads from the pipe and writes to the standard output
	snprintf(cmdLine, sizeof(cmdLine)-1, "main.exe --interactive-first --log-disable --n-gpu-layers 15000 -m models\\%s", config.model);
	if (!CreateProcessA("drivers\\llamacpp\\main.exe", cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &start_info, &process_info)) {
		printf("failed to create process\n");
		exit(-1);
	}
	sleep(3);
	CloseHandle(stdout_WRITE);
	CloseHandle(stdin_READ);
}

eventloop_t *eventloop_create(pipe_t stdin_pipe)
{
	eventloop_t *eventloop = (eventloop_t *)zmalloc(sizeof(*eventloop));
	return (eventloop);
}

void init_os(void)
{
	WSADATA wsaData;
	WORD    versionRequested = MAKEWORD(2, 2);
	WSAStartup(versionRequested, &wsaData);

	InitializeCriticalSection(&thread_mutex);
}
#endif
