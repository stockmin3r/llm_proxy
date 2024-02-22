#include <llm_proxy.h>
/*
std::lock_guard<std::mutex> lock(chunksMutex);

void ProducerThreadFunction() {
    while (...) {
        std::string chunk;
        // Fill `chunk` with the contents produced...
        
        // Lock the chunks vector
        std::lock_guard<std::mutex> lock(chunksMutex);
        
        // Append the chunk to the chunks vector
        chunks.push_back(chunk);
    }
}*/

#ifdef __WINDOWS__

/**
 * @brief Process LLM tokens received from a pipe.
 * @param thread Pointer to the thread structure.
 * @param llm_proxy_stdout Pipe for receiving LLM tokens.
 */
void process_llm_tokens(struct thread *thread, pipe_t llm_proxy_stdout)
{
	struct query *query = thread->query;
	char          token[512];
	char         *p;
	int           last_token = 0;
	DWORD         bytesRead;
	OVERLAPPED    ov = {};

	ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	printf("process llm tokens\n");
	memset(token, 0, sizeof(token));

	pipeEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
    DWORD MAX_READ_TIME_MS = 2000; // Define desirable timeout threshold
	for (;;)
	{
	    DWORD waitedTime = WaitForSingleObject(pipeEventHandle, MAX_READ_TIME_MS);
	    if (waitedTime == WAIT_OBJECT_0 || waitedTime == WAIT_TIMEOUT)
	    {
	        BOOL hasNewData = TRUE;/* Write your condition to ascertain fresh data */;
	        while (hasNewData) {
				DWORD transferredBytes = 0;
				if (ReadFile(g_hChildStd_OUT_Rd, chBuf, sizeof(chBuf), &transferredBytes, NULL))
					hasNewData = TRUE;
					printf("token: %.10s bytesRead: %d\n", token, bytesRead);
			
					if (!bytesRead)
						return;
			
					token[bytesRead] = 0;
			/*		if ((p=strstr(token, "\n> ")) && !strstr(token, "\n> >")) {
						*p = 0;
						bytesRead -= strlen(p);
						last_token = 1;
					}*/
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
					if (query->token_handler && strlen(query->tokens) > 64)
						query->token_handler(query);
					if (last_token)
						break;
					memset(token, 0, bytesRead);
				} else {
	                DWORD errCode = GetLastError();
	                if (errCode != ERROR_IO_PENDING)
						return;
					hasNewData = FALSE;
				}
			}
		}
	}

	while (1) {
		ReadFile(llm_proxy_stdout, token, sizeof(token)-1, &bytesRead, &ov);

		printf("token: %.10s bytesRead: %d\n", token, bytesRead);

		if (!bytesRead)
			return;

		token[bytesRead] = 0;
/*		if ((p=strstr(token, "\n> ")) && !strstr(token, "\n> >")) {
			*p = 0;
			bytesRead -= strlen(p);
			last_token = 1;
		}*/
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
		if (query->token_handler && strlen(query->tokens) > 64)
			query->token_handler(query);
		if (last_token)
			break;
		memset(token, 0, bytesRead);
	}
}
#endif

#ifdef __LINUX__
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
#endif
