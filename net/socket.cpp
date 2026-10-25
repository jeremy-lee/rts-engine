// RTS Engine — net/socket.cpp
#include "socket.h"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <cstring>

namespace rts {

namespace {
bool g_wsaReady = false;
}

bool NetInit() {
	if (g_wsaReady) return true;
	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
	g_wsaReady = true;
	return true;
}

void NetShutdown() {
	if (g_wsaReady) {
		WSACleanup();
		g_wsaReady = false;
	}
}

TcpSocket::~TcpSocket() { Close(); }

void TcpSocket::Close() {
	if (m_socket != ~0ull) {
		closesocket(static_cast<SOCKET>(m_socket));
		m_socket = ~0ull;
	}
}

bool TcpSocket::Listen(uint16_t port, int backlog) {
	Close();
	const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return false;

	// 允许地址复用
	int opt = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
		closesocket(s);
		return false;
	}
	if (listen(s, backlog) == SOCKET_ERROR) {
		closesocket(s);
		return false;
	}
	m_socket = static_cast<uint64_t>(s);
	return true;
}

std::unique_ptr<TcpSocket> TcpSocket::Accept() {
	if (m_socket == ~0ull) return nullptr;
	const SOCKET s = accept(static_cast<SOCKET>(m_socket), nullptr, nullptr);
	if (s == INVALID_SOCKET) return nullptr;
	// 禁用 Nagle（命令包小且要求低延迟）
	int flag = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
	return std::unique_ptr<TcpSocket>(new TcpSocket(static_cast<uint64_t>(s)));
}

bool TcpSocket::Connect(const std::string& host, uint16_t port) {
	Close();
	const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return false;

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
		closesocket(s);
		return false;
	}
	if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
		closesocket(s);
		return false;
	}
	int flag = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
	m_socket = static_cast<uint64_t>(s);
	return true;
}

bool TcpSocket::Send(const void* data, size_t size) {
	if (m_socket == ~0ull || data == nullptr || size == 0) return false;
	size_t sent = 0;
	const char* p = static_cast<const char*>(data);
	while (sent < size) {
		const int n = send(static_cast<SOCKET>(m_socket), p + sent,
			static_cast<int>(size - sent), 0);
		if (n <= 0) return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

bool TcpSocket::Recv(void* data, size_t size) {
	if (m_socket == ~0ull || data == nullptr || size == 0) return false;
	size_t got = 0;
	char* p = static_cast<char*>(data);
	while (got < size) {
		const int n = recv(static_cast<SOCKET>(m_socket), p + got,
			static_cast<int>(size - got), 0);
		if (n <= 0) return false;
		got += static_cast<size_t>(n);
	}
	return true;
}

} // namespace rts

#endif // _WIN32
