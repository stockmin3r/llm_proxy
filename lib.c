#include <llm_proxy.h>

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

#ifdef __LINUX__
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