#include <llm_proxy.h>

char                   *index_html;
int                     index_html_size;
unsigned char          *biopanda_png;
unsigned int            biopanda_png_size;
unsigned char          *lawpanda_png;
unsigned int            lawpanda_png_size;
unsigned char          *drpanda_png;
unsigned int            drpanda_png_size;
unsigned char          *codepanda_png;
unsigned int            codepanda_png_size;

void http_get_prompt(int fd, char *request)
{
	struct query  *query;
	char           json[64 KB];
	char           tokens[64 KB];
	char          *prompt_id, *p;
	int            json_size, tokens_size;

	p = strstr(request, "prompt/");
	if (!p)
		return;

	prompt_id = p + 7;
	p = strchr(prompt_id, ' ');
	if (!p)
		return;
	*p = 0;

	mutex_lock(&thread_mutex);
	for (int x = 0; x<config.nr_model_instances; x++) {
		query = threads[x]->query;
		if (!query)
			continue;
		if (!strcmp(query->id, prompt_id)) {
			mutex_lock(&query->query_lock);
			tokens_size = snprintf(tokens, sizeof(tokens)-1, "{\"tokens\":\"%s\"}", query->tokens);
			json_size   = snprintf(json,   sizeof(json)-1,  HTTP_JSON, tokens_size);
			memcpy(json+json_size, tokens, tokens_size);
			send(fd, json, json_size+tokens_size, 0);
			memset(query->tokens, 0, query->tokens_size);
			query->tokens_size = 0;
			mutex_unlock(&query->query_lock);
			break;
		}
	}
	mutex_unlock(&thread_mutex);
}

void http_post_prompt(int fd, char *request)
{
	struct query *query;
	char          response[512];
	char          json[256];
	char          prompt_id[8], *prompt, *p;
	int           response_size, json_size;

	if (!(prompt=strstr(request, "prompt\"")))
		return;
	prompt += 7;
	p = strchr(prompt, '\"');
	if (!p)
		return;
	*p++ = '\n';
	*p   = 0;

	query                  = (struct query *)malloc(sizeof(*query));
	query->question        = strdup(prompt);
	query->tokens          = (char *)malloc(8192);
	query->tokens_size     = 0;
	query->max_tokens_size = 8192;
	query->output_fd       = -1;
	query->token_handler   = NULL;

	random_string(prompt_id);
	query->id     = strdup(prompt_id);
	llm_add_query(query);
	json_size     = snprintf(json, sizeof(json)-1, "{\"prompt_id\":\"%s\"}", prompt_id);
	response_size = snprintf(response, 128, HTTP_JSON, json_size);
	memcpy(response+response_size, json, json_size);
	send(fd, response, response_size+json_size, 0);
}

void http_set_model(int fd, char *request)
{


}

void http_send_img(int fd, char *request)
{
	// GET /img/drpanda.png HTTP/1.1\r\n
	char *image = request+9;

	if (!strncmp(image, "drpanda", 7))
		send(fd, (char *)drpanda_png, drpanda_png_size, 0);
	else if (!strncmp(image, "lawpanda", 8))
		send(fd, (char *)lawpanda_png, lawpanda_png_size, 0);
	else if (!strncmp(image, "biopanda", 8))
		send(fd, (char *)biopanda_png, biopanda_png_size, 0);
	else if (!strncmp(image, "codepanda", 9))
		send(fd, (char *)codepanda_png, codepanda_png_size, 0);
}

void http_send_models(int fd)
{
	send(fd, models_json, models_json_size, 0);
}

void *http_server(void *args)
{
	struct sockaddr_in srv, cli;
	char               buf[1024];
	char              *mainpage;
	socklen_t          slen   = 16;
	int                val    = 1, sockfd, client_fd, nbytes, mainpage_size;
	unsigned short     port   = 8086;

	mainpage      = (char *)malloc(sizeof(HTTP_GET)+index_html_size+256);
	mainpage_size = snprintf(mainpage, 128, HTTP_GET, index_html_size);
	memcpy(mainpage+mainpage_size, index_html, index_html_size);
	mainpage_size += index_html_size;

	sockfd = net_tcp_bind(LOCALHOST, port);
	if (sockfd == -1)
		exit(-1);
	for (;;) {
		client_fd = accept(sockfd, (struct sockaddr *)&cli, &slen);
		if (client_fd < 0)
			continue;
		nbytes = recv(client_fd, buf, 1024, 0);
		if (nbytes <= 0) {
			close(client_fd);
			continue;
		}
		buf[nbytes] = 0;
		if (!strncmp(buf, "GET ", 4)) {
			if (strstr(buf, "GET /index.html HTTP/1.1")) {
				send(client_fd, mainpage, mainpage_size, 0);
			} else if (strstr(buf, "/set_model/")) {
				// assign model to an LLM slot
				http_set_model(client_fd, buf);
			} else if (strstr(buf, "/img")) {
				http_send_img(client_fd, buf);
			} else if (strstr(buf, "/models")) {
				http_send_models(client_fd);
			} else if (strstr(buf, "/prompt")) {
				http_get_prompt(client_fd, buf);
			}
		} else if (!strncmp(buf, "POST ", 5)) {
			if (strstr(buf, "/prompt")) {
				http_post_prompt(client_fd, buf);
			}
		}
		close(client_fd);
	}
}


void load_image(const char *filename, unsigned char **image, unsigned int *image_size)
{
	struct stat    sb;
	struct filemap filemap;
	char          *png, *buf;
	int            fd, nbytes;

	memset(&filemap, 0, sizeof(filemap));
	png = fs_mapfile_rw((char *)filename, &filemap);
	if (!png)
		exit(-1);
	buf = (char *)malloc(filemap.filesize + sizeof(HTTP_IMAGE)+512);
	if (!buf)
		return;
	nbytes = snprintf(buf, 256, HTTP_IMAGE, (int)filemap.filesize);
	memcpy(buf+nbytes, png, filemap.filesize);
	*image_size = sb.st_size+nbytes;
	*image      = (unsigned char *)buf;
}

void load_html() {
	struct filemap filemap;

	index_html = fs_mapfile_rw((char *)"index.html", &filemap);
	if (!index_html) {
		printf("failed to load index.html\n");
		exit(-1);
	}
	index_html_size = filemap.filesize;
}

void init_http(void)
{
	load_html();
	load_image("codepanda.png", &codepanda_png, &codepanda_png_size);
	load_image("drpanda.png",   &drpanda_png,   &drpanda_png_size);
	load_image("lawpanda.png",  &lawpanda_png,  &lawpanda_png_size);
	load_image("biopanda.png",  &biopanda_png,  &biopanda_png_size);
	thread_create(http_server, NULL);
}

#ifdef __LIBCURL__
struct curldata {
    char         *memory;
    size_t        size;
	size_t        max_size;
};

size_t curl_get_data(char *buf, size_t size, size_t count, void *data)
{
	struct curldata *cdata = (struct curldata *)data;
	char *ptr;
	size_t realsize = size*count;

	ptr = (char *)realloc(cdata->memory, cdata->size + realsize + 1);
	if (!ptr)
		return 0;
	cdata->memory = ptr;
	memcpy(&(cdata->memory[cdata->size]), buf, realsize);
	cdata->size += realsize;
	cdata->memory[cdata->size] = 0;
	return (realsize);
}

char *curl_get(char *url)
{
	CURL              *curl;
	struct curldata    cdata;
	struct curl_slist *headers = NULL;
	int                respones_len;

	cdata.memory   = (char *)malloc(1000 KB);
	cdata.size     = 0;
	cdata.max_size = 1000 KB;
	if (!cdata.memory)
		return NULL;

	headers = curl_slist_append(headers, "Accept-Encoding: gzip");
	curl    = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_get_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cdata);
	curl_easy_setopt(curl, CURLOPT_SSLENGINE_DEFAULT, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	curl_easy_perform(curl);
	printf("mem: %s\n", cdata.memory);
	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);
	return (cdata.memory);
out:
	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);
	free(cdata.memory);
	return (NULL);
}
#endif