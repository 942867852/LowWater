extends SceneTree
func _initialize():
	var vi = Engine.get_version_info()
	print("LOWWATER_SPIKE_OK version=", vi.string)
	print("display_driver=", DisplayServer.get_name())
	var s: int = 20250910
	var h: int = 1469598103934665603
	for i in range(1000):
		s = s * 6364136223846793005 + 1442695040888963407
		var b: int = (s >> 33) & 0xFF
		h = (h ^ b) * 1099511628211
	print("hash=", h)
	quit()
