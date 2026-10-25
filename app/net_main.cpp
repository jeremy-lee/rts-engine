// RTS Engine — app/net_main.cpp
// M7 联机 demo：主机（AI 玩家0）vs 客户端（AI 玩家1），锁步同步。
// 用法：
//   主机：rts_net.exe host [端口] [tick数]
//   客户端：rts_net.exe client [主机IP] [端口] [tick数]
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <thread>

#include "core/core.h"
#include "net/socket.h"
#include "net/lockstep.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"

using namespace rts;

namespace {

const Rules kRules = Rules::FromYAML(R"(
units:
  harvester:
    name: "采矿车"
    cost: 100
    hp: 150
    speed: 28
    harvester: true
    weapon:
      damage: 0
      range: 0
      cooldown: 60
  infantry:
    name: "步兵"
    cost: 50
    buildTime: 10
    hp: 100
    speed: 32
    weapon:
      damage: 10
      range: 4096
      cooldown: 15
  tank:
    name: "坦克"
    cost: 150
    buildTime: 30
    hp: 300
    speed: 20
    weapon:
      damage: 25
      range: 5120
      cooldown: 30
buildings:
  barracks:
    name: "兵营"
    cost: 300
    buildTime: 20
    hp: 500
    width: 4
    height: 4
    produces:
      - infantry
      - tank
)");

Map NetMap() {
	Map map(128, 128, 0, kWalkable | kBuildable);
	map.SetResourceAmount(CellCoord(20, 20), 8000);
	map.SetResourceAmount(CellCoord(100, 100), 8000);
	return map;
}

// 运行联机会话（host 或 client），返回最终哈希
uint64_t RunSession(bool isHost, const char* hostAddr, uint16_t port, int ticks) {
	LockstepSession session;
	if (isHost) {
		if (!session.StartHost(port, 1)) {
			std::printf("[host] 启动失败（端口被占用？）\n");
			return 0;
		}
		std::printf("[host] 已监听端口 %u，客户端已连接\n", port);
	} else {
		if (!session.StartClient(hostAddr, port)) {
			std::printf("[client] 连接失败\n");
			return 0;
		}
		std::printf("[client] 已连接，playerId=%u\n", session.LocalPlayerId());
	}

	Simulation sim(NetMap(), 0x2024, kRules);
	sim.GetWorld().AddCredits(0, 2000);
	sim.GetWorld().AddCredits(1, 2000);

	// 本地 AI（每个玩家只运行自己的 AI，命令通过锁步同步）
	AIPlayer localAI(session.LocalPlayerId(), 100 + session.LocalPlayerId());

	uint64_t lastHash = 0;
	for (int t = 0; t < ticks; ++t) {
		// 1) 本地 AI 产生命令（基于本地世界状态）
		CommandQueue localQueue;
		localAI.Update(sim.GetWorld(), localQueue);
		std::vector<Command> local;
		localQueue.Consume(local);

		// 2) 锁步同步：提交本地命令，获取所有玩家合并命令
		const auto merged = session.Step(static_cast<Tick>(t), local);
		for (const auto& cmd : merged) sim.Commands().Push(cmd);
		sim.Step();

		if (t % 300 == 0 || t == ticks - 1) {
			lastHash = sim.GetWorld().StateHash();
			std::printf("[%s] tick %d/%d hash=%016llx credits=%d/%d\n",
				isHost ? "host" : "client", t, ticks,
				static_cast<unsigned long long>(lastHash),
				sim.GetWorld().Credits(0), sim.GetWorld().Credits(1));
		}
	}
	return sim.GetWorld().StateHash();
}

} // namespace

int main(int argc, char** argv) {
	NetInit();
	if (argc < 2) {
		std::printf("用法:\n  rts_net host [port] [ticks]\n  rts_net client <hostIP> [port] [ticks]\n");
		return 1;
	}
	const bool isHost = (std::strcmp(argv[1], "host") == 0);
	const uint16_t port = 39010;
	int ticks = 2000;

	uint64_t hash = 0;
	if (isHost) {
		if (argc >= 3) ticks = std::atoi(argv[2]);
		hash = RunSession(true, nullptr, port, ticks);
	} else {
		if (argc < 3) {
			std::printf("客户端需要主机 IP\n");
			return 1;
		}
		const char* addr = argv[2];
		if (argc >= 4) ticks = std::atoi(argv[3]);
		hash = RunSession(false, addr, port, ticks);
	}

	std::printf("最终哈希: %016llx\n", static_cast<unsigned long long>(hash));
	NetShutdown();
	return 0;
}
