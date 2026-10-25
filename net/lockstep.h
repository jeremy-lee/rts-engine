// RTS Engine — net/lockstep.h
// 确定性锁步会话：
//   Host：监听客户端，每 tick 收集所有玩家命令 → 合并 → 广播
//   Client：连接主机，每 tick 发送自己的命令 → 接收合并命令
// 协议（小端）：
//   MSG_HELLO: [u8 type=0][u8 playerId]
//   MSG_TICK : [u8 type=1][i64 tick][u16 count][Command * count]
//   MSG_HASH : [u8 type=2][i64 tick][u64 hash]
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "socket.h"
#include "core/core.h"
#include "sim/command.h"

namespace rts {

class LockstepSession {
public:
	LockstepSession() = default;
	~LockstepSession();

	LockstepSession(const LockstepSession&) = delete;
	LockstepSession& operator=(const LockstepSession&) = delete;

	// 主机：监听并等待 clientCount 个客户端连接（阻塞）
	bool StartHost(uint16_t port, int clientCount);
	// 客户端：连接主机（阻塞）
	bool StartClient(const std::string& host, uint16_t port);

	bool IsHost() const { return m_isHost; }
	bool Valid() const { return m_valid; }
	uint8_t LocalPlayerId() const { return m_localPlayer; }
	int PlayerCount() const { return m_playerCount; }

	// 每逻辑 tick 调用：提交本地命令，返回合并后的命令（按 playerId 顺序）
	// Host 返回合并结果；Client 返回主机广播的合并结果。
	std::vector<Command> Step(Tick tick, const std::vector<Command>& localCommands);

	// 哈希校验：Client 上报，Host 汇总
	void SendHash(Tick tick, uint64_t hash);
	bool TryRecvHashFromClient(int clientIndex, Tick& tick, uint64_t& hash);

	void Close();

private:
	void FreeClients();
	bool SendTick(TcpSocket& sock, Tick tick, const std::vector<Command>& cmds);
	bool RecvTick(TcpSocket& sock, Tick& tick, std::vector<Command>& cmds);

	bool m_isHost = false;
	bool m_valid = false;
	uint8_t m_localPlayer = 0;
	int m_playerCount = 1;

	std::unique_ptr<TcpSocket> m_listener;
	std::vector<std::unique_ptr<TcpSocket>> m_clients; // Host 视角的客户端
	std::unique_ptr<TcpSocket> m_server;               // Client 视角的主机
};

} // namespace rts

#endif // _WIN32
