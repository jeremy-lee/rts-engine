// RTS Engine — net/socket.h
// Windows TCP socket 封装（Winsock2，零第三方依赖）。
// 阻塞模式，用于锁步联机的命令同步。
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <string>

namespace rts {

class TcpSocket {
public:
	TcpSocket() = default;
	~TcpSocket();

	TcpSocket(const TcpSocket&) = delete;
	TcpSocket& operator=(const TcpSocket&) = delete;

	// 服务端：监听端口
	bool Listen(uint16_t port, int backlog = 4);
	// 服务端：接受一个连接（阻塞），失败返回 nullptr
	std::unique_ptr<TcpSocket> Accept();
	// 客户端：连接主机
	bool Connect(const std::string& host, uint16_t port);

	bool Send(const void* data, size_t size);
	bool Recv(void* data, size_t size); // 阻塞直到读满或断开

	void Close();
	bool IsValid() const { return m_socket != ~0ull; }

private:
	explicit TcpSocket(uint64_t sock) : m_socket(sock) {}
	uint64_t m_socket = ~0ull;
};

// Winsock 全局初始化（程序启动时调用一次）
bool NetInit();
void NetShutdown();

} // namespace rts

#endif // _WIN32
