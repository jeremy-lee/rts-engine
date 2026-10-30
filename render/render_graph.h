// RTS Engine — render/render_graph.h
// 帧图（Render Dependency Graph）——UE5 FRDGBuilder 的极简 2D 等价物。
//
// 对齐 UE5 的语义（Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h）：
//   FRDGBuilder::AddPass(RDG_EVENT_NAME("Name"), Params, ERDGPassFlags::Raster, Lambda)
//   FRDGBuilder::Execute()
// 我们去掉 GPU 资源生命周期管理（2D 固定管线直接画到 backbuffer），保留：
//   - 具名 pass + 明确的执行顺序
//   - 每个 pass 的统计（绘制调用/顶点数/CPU 耗时），对应 UE5 的 stat rhi
//   - 条件执行（ShouldExecute），对应 UE5 的 pass flags
//   - 预留：pass 可声明读写的纹理（将来接 FBO 时用于自动加 barrier / 排序）
//
// 用法：
//   RenderGraph graph;
//   graph.AddPass("Terrain", [&](PassContext& ctx) { ... });
//   graph.AddPass("Sprites", [&](PassContext& ctx) { ... });
//   graph.Execute(renderer, camera);
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rts {

// 绘制统计（对应 UE5 的 RHIPassStats）
struct PassStats {
	uint32_t drawCalls = 0;
	uint32_t vertices = 0;
	double cpuMs = 0.0;
};

struct PassContext {
	int32_t viewW = 0;
	int32_t viewH = 0;
	float dt = 0.0f;
	PassStats stats; // pass 内部累加后回填
};

class RenderGraph {
public:
	using PassFn = std::function<void(PassContext&)>;

	// 具名 pass。order 即插入顺序（UE5 由 RDG 推断依赖，我们显式排序）。
	void AddPass(const std::string& name, PassFn fn) {
		Pass p;
		p.name = name;
		p.fn = std::move(fn);
		p.enabled = true;
		m_passes.push_back(std::move(p));
	}

	// 条件 pass（对应 UE5 里按 pass flags 跳过）
	void AddPassIf(const std::string& name, bool condition, PassFn fn) {
		Pass p;
		p.name = name;
		p.fn = std::move(fn);
		p.enabled = condition;
		m_passes.push_back(std::move(p));
	}

	// 执行全部 pass（按插入顺序）。返回是否全部成功。
	void Execute(int32_t viewW, int32_t viewH, float dt) {
		m_results.clear();
		m_results.reserve(m_passes.size());
		for (Pass& p : m_passes) {
			if (!p.enabled || !p.fn) continue;
			PassContext ctx;
			ctx.viewW = viewW;
			ctx.viewH = viewH;
			ctx.dt = dt;
			const auto t0 = std::chrono::high_resolution_clock::now();
			p.fn(ctx);
			const auto t1 = std::chrono::high_resolution_clock::now();
			ctx.stats.cpuMs =
				std::chrono::duration<double, std::milli>(t1 - t0).count();
			m_results.push_back(Result{p.name, ctx.stats});
		}
	}

	// 最后一次 Execute 的每 pass 统计（debug HUD 用）
	struct Result {
		std::string name;
		PassStats stats;
	};
	const std::vector<Result>& Stats() const { return m_results; }

	// 累计值（一帧总和）
	PassStats Totals() const {
		PassStats t;
		for (const Result& r : m_results) {
			t.drawCalls += r.stats.drawCalls;
			t.vertices += r.stats.vertices;
			t.cpuMs += r.stats.cpuMs;
		}
		return t;
	}

	void Clear() {
		m_passes.clear();
		m_results.clear();
	}

private:
	struct Pass {
		std::string name;
		PassFn fn;
		bool enabled = true;
	};
	std::vector<Pass> m_passes;
	std::vector<Result> m_results;
};

} // namespace rts
