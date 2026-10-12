extends SceneTree
func _initialize():
	var sim = LowWaterSim.new()
	print("LOWWATER_BRIDGE_OK")
	print("engine=", Engine.get_version_info()["string"])
	print("game_version=", sim.game_version())
	print("schema_version=", sim.schema_version())
	print("stream_registry_version=", sim.stream_registry_version())
	var h1 = sim.world_hash(20250913, 1440)
	var h2 = sim.world_hash(20250913, 1440)
	print("world_hash(20250913,1440)=", h1)
	print("repeat_identical=", h1 == h2)
	quit()
