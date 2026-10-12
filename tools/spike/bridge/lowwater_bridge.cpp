// ============================================================================
// tools/spike/bridge/lowwater_bridge.cpp
// ----------------------------------------------------------------------------
// 最小 GDExtension 桥接（验收 ADR-001 §8 / godot-cpp-bridge.md 的第 4 节）：
//   用 godot-cpp（不是裸 C API）注册一个类 LowWaterSim，
//   从 GDScript 能调到【simcore 导出的函数】，并且 simcore 侧不引任何引擎头。
//
// 构建：MSVC 19.44 / C++17 / 与 godot-cpp 4.5-stable 完全相同的开关
//   （/utf-8 必需；/MT 与 godot-cpp 一致；-D_HAS_EXCEPTIONS=0）
// ============================================================================
#include <cstdint>

#include <godot_cpp/godot.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// simcore 是【引擎无关】的 header-only 库；本 TU 是唯一允许同时看到两侧的地方。
#define SIMCORE_NO_POISON_RAND 1
#include "simcore/simcore.h"

using namespace godot;

class LowWaterSim : public RefCounted {
	GDCLASS(LowWaterSim, RefCounted);

protected:
	static void _bind_methods();

public:
	LowWaterSim() {}
	~LowWaterSim() override {}

	// --- 以下三个都是 simcore 导出的东西，只是经 godot-cpp 绑定暴露给 GDScript ---
	String game_version() const { return String(simcore::kGameVersion); }
	int64_t schema_version() const { return static_cast<int64_t>(simcore::kSimSchemaVersion); }
	int64_t stream_registry_version() const {
		return static_cast<int64_t>(simcore::kStreamRegistryVersion);
	}

	/// 用 simcore 的 ADR-002 RNG 流（pcg32_at + deriveSeed64）折出一个 worldHash。
	/// 用途：证明「GDScript -> godot-cpp -> simcore」整条链路通，且值可复现。
	int64_t world_hash(int64_t seed, int64_t ticks) const {
		uint64_t h = 1469598103934665603ull;   // FNV-1a 64 offset basis
		for (int64_t t = 0; t < ticks; ++t) {
			const uint64_t seed64 = simcore::deriveSeed64(
				static_cast<uint64_t>(seed), static_cast<simcore::StreamId>(0), 0);
			const uint32_t v = simcore::pcg32_at(seed64, static_cast<uint64_t>(t) * 4096ull);
			h = (h ^ static_cast<uint64_t>(v)) * 1099511628211ull;
		}
		return static_cast<int64_t>(h);
	}
};

void LowWaterSim::_bind_methods() {
	ClassDB::bind_method(D_METHOD("game_version"), &LowWaterSim::game_version);
	ClassDB::bind_method(D_METHOD("schema_version"), &LowWaterSim::schema_version);
	ClassDB::bind_method(D_METHOD("stream_registry_version"), &LowWaterSim::stream_registry_version);
	ClassDB::bind_method(D_METHOD("world_hash", "seed", "ticks"), &LowWaterSim::world_hash, DEFVAL(600));
}

// ---------------------------------------------------------------------------
// 模块初始化 / 入口
// ---------------------------------------------------------------------------
using namespace godot;

void initialize_lowwater_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ClassDB::register_class<LowWaterSim>();
}

void uninitialize_lowwater_module(ModuleInitializationLevel p_level) {
	(void)p_level;
}

extern "C" {
GDExtensionBool GDE_EXPORT lowwater_bridge_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		const GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_lowwater_module);
	init_obj.register_terminator(uninitialize_lowwater_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
