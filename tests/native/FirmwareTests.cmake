# Linux-only host tests may include device headers. Keep the explicit list shared
# with the Windows boundary audit; no wildcard exempts new portable test sources.
set(firmware_test_targets
  firmware_controls_test
  firmware_history_test
  firmware_graph_snapshot_test
  firmware_graph_projection_test
  firmware_frame_raster_test
  firmware_graph_renderer_test
  firmware_display_frame_test
  firmware_display_tiles_test)
