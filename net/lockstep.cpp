// RTS Engine — net/lockstep.cpp
#include "lockstep.h"

#ifdef _WIN32

#include <algorithm>
#include <cstdio>

namespace rts {

namespace {
constexpr uint8_t kMsgHello = 0;
constexpr uint8_t kMsgTick = 1;
constexpr uint8_t kMsgHash = 2;

constexpr size_t kCmdSize = sizeof(Command); // 20
}

LockstepSession::~LockstepSession() { Close(); }

void LockstepSession::Close() {
	FreeClients();
	m_listener.reset();
	m_server.reset();
	m_valid = false;
}

void LockstepSession::FreeClients() {
	for (auto& c : m_clients) c->Close();
	m_clients.clear();
}

bool LockstepSession::StartHost(uint16_t port, int clientCount) {
	NetInit();
	m_isHost = true;
	m_localPlayer = 0;
	m_playerCount = 1 + clientCount;

	m_listener = std::make_unique<TcpSocket>();
	if (!m_listener->Listen(port)) {
		m_valid = false;
		return false;
	}

	// 等待客户端连接，并分配 playerId
	for (int i = 0; i < clientCount; ++i) {
		auto client = m_listener->Accept();
		if (!client) {
			m_valid = false;
			return false;
		}
		const uint8_t playerId = static_cast<uint8_t>(i + 1);
		uint8_t hello[2] = {kMsgHello, playerId};
		if (!client->Send(hello, sizeof(hello))) {
			m_valid = false;
			return false;
		}
		m_clients.push_back(std::move(client));
	}
	m_valid = true;
	return true;
}

bool LockstepSession::StartClient(const std::string& host, uint16_t port) {
	NetInit();
	m_isHost = false;
	m_server = std::make_unique<TcpSocket>();
	if (!m_server->Connect(host, port)) {
		m_valid = false;
		return false;
	}
	uint8_t hello[2] = {0, 0};
	if (!m_server->Recv(hello, sizeof(hello)) || hello[0] != kMsgHello) {
		m_valid = false;
		return false;
	}
	m_localPlayer = hello[1];
	m_playerCount = 0; // 客户端不知道总数，由 tick 包数量推断（此处不依赖）
	m_valid = true;
	return true;
}

bool LockstepSession::SendTick(TcpSocket& sock, Tick tick, const std::vector<Command>& cmds) {
	uint8_t header[1 + 8 + 2];
	header[0] = kMsgTick;
	for (int i = 0; i < 8; ++i) header[1 + i] = static_cast<uint8_t>((tick >> (i * 8)) & 0xFF);
	const uint16_t count = static_cast<uint16_t>(cmds.size());
	header[9] = static_cast<uint8_t>(count & 0xFF);
	header[10] = static_cast<uint8_t>((count >> 8) & 0xFF);
	if (!sock.Send(header, sizeof(header))) return false;
	if (count > 0 && !sock.Send(cmds.data(), static_cast<size_t>(count) * kCmdSize)) return false;
	return true;
}

bool LockstepSession::RecvTick(TcpSocket& sock, Tick& tick, std::vector<Command>& cmds) {
	uint8_t header[1 + 8 + 2];
	if (!sock.Recv(header, sizeof(header))) return false;
	if (header[0] != kMsgTick) return false;
	tick = 0;
	for (int i = 0; i < 8; ++i) tick |= static_cast<Tick>(header[1 + i]) << (i * 8);
	const uint16_t count = static_cast<uint16_t>(header[9]) | (static_cast<uint16_t>(header[10]) << 8);
	cmds.clear();
	cmds.resize(count);
	if (count > 0 && !sock.Recv(cmds.data(), static_cast<size_t>(count) * kCmdSize)) return false;
	return true;
}

std::vector<Command> LockstepSession::Step(Tick tick, const std::vector<Command>& localCommands) {
	std::vector<Command> merged;
	if (!m_valid) return merged;

	if (!m_isHost) {
		// 客户端：发送自己的命令，接收合并结果
		if (!SendTick(*m_server, tick, localCommands)) {
			m_valid = false;
			return merged;
		}
		if (!RecvTick(*m_server, tick, merged)) {
			m_valid = false;
			return merged;
		}
		return merged;
	}

	// 主机：收集所有客户端本 tick 命令
	std::vector<std::vector<Command>> perPlayer;
	perPlayer.push_back(localCommands);
	for (auto& client : m_clients) {
		Tick ctick = 0;
		std::vector<Command> cmds;
		if (!RecvTick(*client, ctick, cmds)) {
			m_valid = false;
			return merged;
		}
		perPlayer.push_back(std::move(cmds));
	}

	// 合并（按 playerId 顺序 = 收集顺序）
	for (const auto& cmds : perPlayer) {
		merged.insert(merged.end(), cmds.begin(), cmds.end());
	}

	// 广播合并结果给所有客户端
	for (auto& client : m_clients) {
		if (!SendTick(*client, tick, merged)) {
			m_valid = false;
			return merged;
		}
	}
	return merged;
}

void LockstepSession::SendHash(Tick tick, uint64_t hash) {
	if (!m_valid || m_isHost) return;
	uint8_t buf[1 + 8 + 8];
	buf[0] = kMsgHash;
	for (int i = 0; i < 8; ++i) buf[1 + i] = static_cast<uint8_t>((tick >> (i * 8)) & 0xFF);
	for (int i = 0; i < 8; ++i) buf[9 + i] = static_cast<uint8_t>((hash >> (i * 8)) & 0xFF);
	m_server->Send(buf, sizeof(buf));
}

bool LockstepSession::TryRecvHashFromClient(int clientIndex, Tick& tick, uint64_t& hash) {
	if (!m_valid || !m_isHost) return false;
	if (clientIndex < 0 || clientIndex >= static_cast<int>(m_clients.size())) return false;
	uint8_t buf[1 + 8 + 8];
	if (!m_clients[static_cast<size_t>(clientIndex)]->Recv(buf, sizeof(buf))) return false;
	if (buf[0] != kMsgHash) return false;
	tick = 0;
	for (int i = 0; i < 8; ++i) tick |= static_cast<Tick>(buf[1 + i]) << (i * 8);
	hash = 0;
	for (int i = 0; i < 8; ++i) hash |= static_cast<uint64_t>(buf[9 + i]) << (i * 8);
	return true;
}

} // namespace rts

#endif // _WIN32
