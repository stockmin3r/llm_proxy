#include <llm_proxy.h>

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

/**
 * @brief Replaces all occurrences of a pattern in a string.
 *
 * This function replaces all occurrences of the specified pattern in the given string.
 * It modifies the original string in-place.
 *
 * @param str The string to be modified.
 * @param pattern The pattern to be replaced.
 */
void cstring_strstr_replace(char *str, char *pattern)
{
	char *p, *endp;
	int pattern_len = strlen(pattern), string_len = strlen(str);

	endp = str + string_len;
	while ((p=strstr(str, pattern)) != NULL) {
		memmove(p, p+pattern_len, endp-p-pattern_len);
		str[string_len-pattern_len] = 0;
		str         = p + 1;
		endp       -= pattern_len;
		string_len -= pattern_len;
	}
}

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

#ifdef __LINUX__
/**
 * @brief Maps a file into memory for read-write access on Windows.
 *
 * This function maps the specified file into memory for read-write access on Windows.
 * It returns a pointer to the mapped memory region.
 *
 * @param path The path of the file to be mapped.
 * @param filemap A pointer to the filemap structure to store file-related information.
 * @return A pointer to the mapped memory region, or NULL if an error occurs.
 */
char *fs_mapfile_rw(char *path, struct filemap *filemap)
{
	struct stat sb;
	char *map;
	int fd;

	fd = open(path, O_RDWR|O_CLOEXEC);
	if (fd < 0) {
		filemap->filesize = 0;
		filemap->fd = -1;
		return NULL;
	}
	fstat(fd, &sb);
	if (!sb.st_size) {
		close(fd);
		return NULL;
	}
	map = (char *)mmap(NULL, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == (void *)-1) {
		close(fd);
		return NULL;
	}
	filemap->fd = fd;
	filemap->filesize = sb.st_size;
	return (map);
}
#endif

#ifdef __WINDOWS__
char *fs_mapfile_rw(char *path, struct filemap *filemap)
{
	HANDLE        hFile;
	HANDLE        hMap;
	LPVOID        lpBasePtr;
	LARGE_INTEGER liFileSize;

	hFile = CreateFile(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile == INVALID_HANDLE_VALUE)
		return NULL;

	if (!GetFileSizeEx(hFile, &liFileSize)) {
		CloseHandle(hFile);
		return NULL;
	}

	if (liFileSize.QuadPart == 0) {
		CloseHandle(hFile);
		return NULL;
	}

	hMap = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
	if (hMap == 0) {
		CloseHandle(hFile);
		return NULL;
	}

	lpBasePtr = MapViewOfFile(hMap,FILE_MAP_READ|FILE_MAP_WRITE, 0, 0, 0);
	if (lpBasePtr == NULL) {
		CloseHandle(hMap);
		CloseHandle(hFile);
		return NULL;
	}
	filemap->hMap     = hMap;
	filemap->hFile    = hFile;
	filemap->filesize = liFileSize.QuadPart;
	return ((char *)lpBasePtr);
}
#endif