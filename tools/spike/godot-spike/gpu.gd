extends SceneTree
func _initialize():
	print("display_driver=", DisplayServer.get_name())
	print("adapter=", RenderingServer.get_video_adapter_name())
	print("api=", RenderingServer.get_video_adapter_api_version())
	quit()
