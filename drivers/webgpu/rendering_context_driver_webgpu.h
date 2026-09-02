/**************************************************************************/
/*  rendering_context_driver_webgpu.h                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef WEBGPU_ENABLED

#include "servers/rendering/rendering_context_driver.h"

#include <webgpu/webgpu.h>

class RenderingContextDriverWebGPU : public RenderingContextDriver {
	WGPUInstance instance = nullptr;
	WGPUAdapter adapter = nullptr;
	WGPUDevice device = nullptr;
	WGPUQueue queue = nullptr;

	Device device_info; // Single device description.

	struct Surface {
		WGPUSurface handle = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
		DisplayServer::VSyncMode vsync_mode = DisplayServer::VSYNC_ENABLED;
		bool needs_resize = false;
	};

	HashMap<SurfaceID, Surface> surfaces;
	SurfaceID next_surface_id = 1;

public:
	RenderingContextDriverWebGPU();
	virtual ~RenderingContextDriverWebGPU() override;

	// --- RenderingContextDriver interface ---

	virtual Error initialize() override final;
	virtual const Device &device_get(uint32_t p_device_index) const override final;
	virtual uint32_t device_get_count() const override final;
	virtual bool device_supports_present(uint32_t p_device_index, SurfaceID p_surface) const override final;
	virtual RenderingDeviceDriver *driver_create() override final;
	virtual void driver_free(RenderingDeviceDriver *p_driver) override final;
	virtual SurfaceID surface_create(const void *p_platform_data) override final;
	virtual void surface_set_size(SurfaceID p_surface, uint32_t p_width, uint32_t p_height) override final;
	virtual void surface_set_vsync_mode(SurfaceID p_surface, DisplayServer::VSyncMode p_vsync_mode) override final;
	virtual DisplayServer::VSyncMode surface_get_vsync_mode(SurfaceID p_surface) const override final;
	virtual uint32_t surface_get_width(SurfaceID p_surface) const override final;
	virtual uint32_t surface_get_height(SurfaceID p_surface) const override final;
	virtual void surface_set_needs_resize(SurfaceID p_surface, bool p_needs_resize) override final;
	virtual bool surface_get_needs_resize(SurfaceID p_surface) const override final;
	virtual void surface_destroy(SurfaceID p_surface) override final;
	virtual bool is_debug_utils_enabled() const override final;

	// --- Accessors for RenderingDeviceDriverWebGPU ---

	WGPUDevice get_device() const { return device; }
	WGPUQueue get_queue() const { return queue; }
	WGPUInstance get_instance() const { return instance; }
	WGPUSurface surface_get_handle(SurfaceID p_surface) const;
};

#endif // WEBGPU_ENABLED
