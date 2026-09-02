extends Node
## Autoload benchmark profiler. Collects per-frame CPU and GPU render times
## using RenderingServer.viewport_get_measured_render_time_cpu/gpu().
##
## These measure actual render work, NOT padded by vsync — so they give
## meaningful comparisons even when vsync-locked at 60fps.
##
## GPU time requires timestamp queries:
##   - Metal: returns 0 (known Godot bug #102968)
##   - WebGPU: requires chrome://flags/#enable-webgpu-developer-features
##   - WebGL: returns 0 (no timer queries)

const WARMUP_FRAMES := 180   # 3 seconds at 60fps
const COLLECT_FRAMES := 300  # 5 seconds of data

var _frame := 0
var _vp_rid: RID
var _deltas: PackedFloat64Array
var _render_cpu: PackedFloat64Array
var _render_gpu: PackedFloat64Array
var _draw_calls: PackedFloat64Array
var _primitives: PackedFloat64Array

func _ready() -> void:
	_vp_rid = get_viewport().get_viewport_rid()
	# NOTE: On WebGPU, GPU timestamp queries cause "buffer used in submit while
	# mapped" errors under heavy load (emdawnwebgpu async readback limitation).
	# Only enable on non-web platforms where buffer mapping is synchronous.
	if OS.get_name() != "Web":
		RenderingServer.viewport_set_measure_render_time(_vp_rid, true)
	_deltas.resize(COLLECT_FRAMES)
	_render_cpu.resize(COLLECT_FRAMES)
	_render_gpu.resize(COLLECT_FRAMES)
	_draw_calls.resize(COLLECT_FRAMES)
	_primitives.resize(COLLECT_FRAMES)
	process_priority = -100

func _process(delta: float) -> void:
	_frame += 1

	if _frame <= WARMUP_FRAMES:
		if _frame == 1:
			print("[BENCHMARK] warming up (%d frames)..." % WARMUP_FRAMES)
		return

	var idx := _frame - WARMUP_FRAMES - 1
	if idx < COLLECT_FRAMES:
		_deltas[idx] = delta
		_render_cpu[idx] = RenderingServer.viewport_get_measured_render_time_cpu(_vp_rid)
		_render_gpu[idx] = RenderingServer.viewport_get_measured_render_time_gpu(_vp_rid)
		_draw_calls[idx] = Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)
		_primitives[idx] = Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)
		if idx == 0:
			print("[BENCHMARK] collecting %d frames..." % COLLECT_FRAMES)
	elif idx == COLLECT_FRAMES:
		_print_results()

func _print_results() -> void:
	var n := COLLECT_FRAMES

	var sorted_delta := _deltas.duplicate()
	sorted_delta.sort()
	var sorted_cpu := _render_cpu.duplicate()
	sorted_cpu.sort()
	var sorted_gpu := _render_gpu.duplicate()
	sorted_gpu.sort()

	var avg_delta := _avg(_deltas)
	var avg_cpu := _avg(_render_cpu)
	var avg_gpu := _avg(_render_gpu)
	var avg_dc := _avg(_draw_calls)
	var avg_prims := _avg(_primitives)

	var gpu_available := avg_gpu > 0.001

	print("")
	print("[BENCHMARK] ============ RESULTS (%d frames) ============" % n)
	print("[BENCHMARK] fps_avg=%.1f" % (1.0 / avg_delta))
	print("[BENCHMARK]")
	print("[BENCHMARK] --- Frame Time (wall clock) ---")
	print("[BENCHMARK] frame_avg=%.2f ms" % (avg_delta * 1000.0))
	print("[BENCHMARK] frame_p50=%.2f ms" % (sorted_delta[n / 2] * 1000.0))
	print("[BENCHMARK] frame_p95=%.2f ms" % (sorted_delta[int(n * 0.95)] * 1000.0))
	print("[BENCHMARK] frame_p99=%.2f ms" % (sorted_delta[int(n * 0.99)] * 1000.0))
	print("[BENCHMARK] frame_min=%.2f ms" % (sorted_delta[0] * 1000.0))
	print("[BENCHMARK] frame_max=%.2f ms" % (sorted_delta[n - 1] * 1000.0))
	print("[BENCHMARK]")
	print("[BENCHMARK] --- CPU Render Time ---")
	print("[BENCHMARK] cpu_avg=%.2f ms" % avg_cpu)
	print("[BENCHMARK] cpu_p50=%.2f ms" % sorted_cpu[n / 2])
	print("[BENCHMARK] cpu_p95=%.2f ms" % sorted_cpu[int(n * 0.95)])
	print("[BENCHMARK] cpu_p99=%.2f ms" % sorted_cpu[int(n * 0.99)])
	print("[BENCHMARK]")
	if gpu_available:
		print("[BENCHMARK] --- GPU Render Time (timestamp queries) ---")
		print("[BENCHMARK] gpu_avg=%.2f ms" % avg_gpu)
		print("[BENCHMARK] gpu_p50=%.2f ms" % sorted_gpu[n / 2])
		print("[BENCHMARK] gpu_p95=%.2f ms" % sorted_gpu[int(n * 0.95)])
		print("[BENCHMARK] gpu_p99=%.2f ms" % sorted_gpu[int(n * 0.99)])
		print("[BENCHMARK] gpu_min=%.2f ms" % sorted_gpu[0])
		print("[BENCHMARK] gpu_max=%.2f ms" % sorted_gpu[n - 1])
	else:
		print("[BENCHMARK] --- GPU Render Time ---")
		print("[BENCHMARK] gpu=N/A (timestamp queries unavailable)")
		print("[BENCHMARK] (Metal: known bug #102968. WebGPU: enable chrome://flags/#enable-webgpu-developer-features)")
	print("[BENCHMARK]")
	print("[BENCHMARK] --- Rendering ---")
	print("[BENCHMARK] draw_calls_avg=%.0f" % avg_dc)
	print("[BENCHMARK] primitives_avg=%.0f" % avg_prims)
	print("[BENCHMARK] ================================================")
	print("")

func _avg(arr: PackedFloat64Array) -> float:
	var sum := 0.0
	for v in arr:
		sum += v
	return sum / arr.size()
