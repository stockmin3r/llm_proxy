#ifndef __HTTP_HPP
#define __HTTP_HPP

#include <llm_proxy.h>

using RouteHandler = std::function<void(SOCKET, const std::string&, const std::string&)>;
using RouteMap     = std::map<std::string, RouteHandler>;

class HTTP_SERVER {
public:
	virtual ~HTTP_SERVER() {};
	virtual bool HttpInit() = 0;
	virtual void Run()      = 0;
protected:
	virtual void HttpAccept(ULONG_PTR Key, ULONG IoSize, LPOVERLAPPED_EX pov) = 0;
	virtual void HttpRecv  (ULONG_PTR Key, ULONG IoSize, LPOVERLAPPED_EX pov) = 0;
};

#ifdef __WINDOWS__

class IOCP_HTTP_SERVER : public HTTP_SERVER {
public:
	IOCP_HTTP_SERVER();
	~IOCP_HTTP_SERVER();
	bool HttpInit();
	void Run();
protected:
	void HttpAccept(ULONG_PTR Key, ULONG IoSize, LPOVERLAPPED_EX pov) override;
	void HttpRecv  (ULONG_PTR Key, ULONG IoSize, LPOVERLAPPED_EX pov) override;
	void HttpServerThread(void);
	WSADATA m_WSAData;
	SOCKET  m_ListenerSocket;
	HANDLE  m_hCompletionPort;
	ULONG   g_RefCount;
};

IOCP_HTTP_SERVER::IOCP_HTTP_SERVER() : m_WSAData{}, m_ListenerSocket{ INVALID_SOCKET }, m_hCompletionPort{ INVALID_HANDLE_VALUE }, g_RefCount{ 0 } {}
#endif

#ifdef __LINUX__
class EPOLL_HTTP_SERVER : public HTTP_SERVER {
public:
	EPOLL_HTTP_SERVER();
	~EPOLL_HTTP_SERVER();
	bool HttpInit();
protected:
	void HttpAccept(void) override;
	void HttpRecv  (void) override;

	WSADATA m_WSAData;
	SOCKET  m_ListenerSocket;
	HANDLE  m_hCompletionPort;
	ULONG   g_RefCount;
};
#endif

#endif