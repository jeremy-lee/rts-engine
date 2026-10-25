// RTS Engine — tests/test_net.cpp
// M7 验收：锁步联机（回环双玩家）命令同步 + 状态哈希一致
#include "test_framework.h"
#include "net/lockstep.h"
#include "net/socket.h"
#include "sim/simulation.h"
#include "sim/gamedefs.h"

#include <cstdio>
#include <thread>

using namespace rts;

namespace {

Rules NetRules() {
	return Rules::FromYAML(R"(
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
)");
}

Map NetMap() {
	return Map(64, 64, 0, kWalkable | kBuildable);
}

} // namespace

TEST(net_lockstep_two_players_sync) {
	NetInit();
	const uint16_t port = 39001;
	constexpr int kTicks = 120;

	uint64_t hostHash = 0;
	uint64_t clientHash = 0;
	bool hostOk = false;
	bool clientOk = false;

	const Rules rules = NetRules();

	// 主机线程
	std::thread hostThread([&]() {
		LockstepSession host;
		if (!host.StartHost(port, 1)) return;
		Simulation sim(NetMap(), 0x1234, rules);
		// 主机本地命令：tick 0 生成一个单位
		for (int t = 0; t < kTicks; ++t) {
			std::vector<Command> local;
			if (t == 0) {
				Command c;
				c.type = CommandType::Spawn;
				c.playerId = 0;
				c.x = 10 * kSubPerCell;
				c.y = 10 * kSubPerCell;
				c.targetId = 1;
				local.push_back(c);
			}
			const auto merged = host.Step(static_cast<Tick>(t), local);
			for (const auto& cmd : merged) sim.Commands().Push(cmd);
			sim.Step();
		}
		hostHash = sim.GetWorld().StateHash();
		hostOk = true;
	});

	// 客户端线程
	std::thread clientThread([&]() {
		LockstepSession client;
		if (!client.StartClient("127.0.0.1", port)) return;
		Simulation sim(NetMap(), 0x1234, rules);
		for (int t = 0; t < kTicks; ++t) {
			std::vector<Command> local;
			if (t == 0) {
				Command c;
				c.type = CommandType::Spawn;
				c.playerId = 1;
				c.x = 50 * kSubPerCell;
				c.y = 50 * kSubPerCell;
				c.targetId = 1;
				local.push_back(c);
			}
			const auto merged = client.Step(static_cast<Tick>(t), local);
			for (const auto& cmd : merged) sim.Commands().Push(cmd);
			sim.Step();
		}
		clientHash = sim.GetWorld().StateHash();
		clientOk = true;
	});

	hostThread.join();
	clientThread.join();

	CHECK(hostOk);
	CHECK(clientOk);
	std::printf("    [net] host hash=%016llx client hash=%016llx\n",
		static_cast<unsigned long long>(hostHash),
		static_cast<unsigned long long>(clientHash));
	CHECK_EQ(hostHash, clientHash);
}

TEST(net_lockstep_hash_message) {
	// 哈希校验消息（Host 接收 Client 上报）
	NetInit();
	const uint16_t port = 39002;
	uint64_t receivedHash = 0;
	bool ok = false;

	std::thread hostThread([&]() {
		LockstepSession host;
		if (!host.StartHost(port, 1)) return;
		// 先跑一个 tick 完成握手
		Command dummy;
		dummy.type = CommandType::None;
		host.Step(0, {dummy});
		Tick tick = 0;
		uint64_t hash = 0;
		if (host.TryRecvHashFromClient(0, tick, hash)) {
			receivedHash = hash;
			ok = true;
		}
	});

	std::thread clientThread([&]() {
		LockstepSession client;
		if (!client.StartClient("127.0.0.1", port)) return;
		// 客户端：发送 tick 0 命令，接收合并
		Command dummy;
		dummy.type = CommandType::None;
		client.Step(0, {dummy});
		// 上报哈希
		client.SendHash(0, 0xDEADBEEFCAFEull);
	});

	hostThread.join();
	clientThread.join();

	CHECK(ok);
	CHECK_EQ(receivedHash, 0xDEADBEEFCAFEull);
}
