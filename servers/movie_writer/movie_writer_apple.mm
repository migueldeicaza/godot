/**************************************************************************/
/*  movie_writer_apple.mm                                                 */
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

#import "movie_writer_apple.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/list.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>

namespace {

static String ns_error_to_string(NSError *p_error) {
	if (p_error == nil) {
		return String();
	}
	String description = String::utf8([[p_error localizedDescription] UTF8String]);
	description += vformat(" (domain=%s code=%d", String::utf8([[p_error domain] UTF8String]), int([p_error code]));
	if ([p_error localizedFailureReason] != nil) {
		description += ", reason=" + String::utf8([[p_error localizedFailureReason] UTF8String]);
	}
	if ([p_error localizedRecoverySuggestion] != nil) {
		description += ", suggestion=" + String::utf8([[p_error localizedRecoverySuggestion] UTF8String]);
	}
	description += ")";
	return description;
}

static String ns_string_to_string(NSString *p_string) {
	if (p_string == nil) {
		return String();
	}
	return String::utf8([p_string UTF8String]);
}

static String get_string_setting(const char *p_path) {
	String value = GLOBAL_GET(p_path);
	return value.to_lower().strip_edges();
}

static int get_int_setting(const char *p_path) {
	return int(GLOBAL_GET(p_path));
}

static bool get_bool_setting(const char *p_path) {
	return bool(GLOBAL_GET(p_path));
}

static String writer_status_to_string(AVAssetWriterStatus p_status) {
	switch (p_status) {
		case AVAssetWriterStatusUnknown:
			return "unknown";
		case AVAssetWriterStatusWriting:
			return "writing";
		case AVAssetWriterStatusCompleted:
			return "completed";
		case AVAssetWriterStatusFailed:
			return "failed";
		case AVAssetWriterStatusCancelled:
			return "cancelled";
	}

	return "invalid";
}

static void debug_log(bool p_enabled, const String &p_message) {
	if (!p_enabled) {
		return;
	}
	print_line("[MovieWriterApple] " + p_message);
}

static constexpr uint32_t MAX_PENDING_VIDEO_FRAMES = 8;
static constexpr uint32_t MAX_PENDING_AUDIO_BLOCKS = 8;
static constexpr uint64_t QUEUE_WAIT_TIMEOUT_MSEC = 30000;
static constexpr uint64_t QUEUE_WAIT_SLEEP_USEC = 1000;

static uint32_t get_channel_count(AudioServer::SpeakerMode p_mode) {
	switch (p_mode) {
		case AudioServer::SPEAKER_MODE_STEREO:
			return 2;
		case AudioServer::SPEAKER_SURROUND_31:
			return 4;
		case AudioServer::SPEAKER_SURROUND_51:
			return 6;
		case AudioServer::SPEAKER_SURROUND_71:
			return 8;
	}

	ERR_FAIL_V(2);
}

// The audio server interleaves channels as `FL FR C LFE RL RR SL SR`, truncated to the mode's
// channel count. CoreAudio orders the channels of a bitmap layout by increasing bit, and the bits
// below are in exactly that order, so a bitmap describes the surround modes directly. The
// predefined surround tags do not: they either label the LFE as a center surround (`ITU_3_1`) or
// place the rears where the audio server puts the sides (`ITU_3_4_1`).
static AudioChannelBitmap get_channel_bitmap(AudioServer::SpeakerMode p_mode) {
	const AudioChannelBitmap fronts = kAudioChannelBit_Left | kAudioChannelBit_Right;
	const AudioChannelBitmap center_lfe = kAudioChannelBit_Center | kAudioChannelBit_LFEScreen;
	// `LeftSurround` is the rear pair, `LeftSurroundDirect` the side pair.
	const AudioChannelBitmap rears = kAudioChannelBit_LeftSurround | kAudioChannelBit_RightSurround;
	const AudioChannelBitmap sides = kAudioChannelBit_LeftSurroundDirect | kAudioChannelBit_RightSurroundDirect;

	switch (p_mode) {
		case AudioServer::SPEAKER_MODE_STEREO:
			return fronts;
		case AudioServer::SPEAKER_SURROUND_31:
			return fronts | center_lfe;
		case AudioServer::SPEAKER_SURROUND_51:
			return fronts | center_lfe | rears;
		case AudioServer::SPEAKER_SURROUND_71:
			return fronts | center_lfe | rears | sides;
	}

	ERR_FAIL_V(fronts);
}

static AudioChannelLayout create_channel_layout(AudioServer::SpeakerMode p_mode) {
	AudioChannelLayout layout = {};
	if (p_mode == AudioServer::SPEAKER_MODE_STEREO) {
		// Stereo is the one mode with an exactly matching predefined tag, which players and
		// encoders accept more widely than the equivalent bitmap.
		layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
		return layout;
	}
	layout.mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelBitmap;
	layout.mChannelBitmap = get_channel_bitmap(p_mode);
	return layout;
}

static NSData *create_channel_layout_data(AudioServer::SpeakerMode p_mode) {
	const AudioChannelLayout layout = create_channel_layout(p_mode);
	return [NSData dataWithBytes:&layout length:sizeof(layout)];
}

static AVFileType get_output_file_type(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	if (extension == "mov") {
		return AVFileTypeQuickTimeMovie;
	}
	if (extension == "mp4") {
		return AVFileTypeMPEG4;
	}
	if (extension == "m4v") {
		return AVFileTypeAppleM4V;
	}
	return nil;
}

static int get_pcm_output_bit_depth(const String &p_audio_codec) {
	if (p_audio_codec == "pcm_s24le") {
		return 24;
	}
	return 16;
}

static int get_audio_source_bit_depth(const String &p_audio_codec) {
	if (p_audio_codec.begins_with("pcm_")) {
		return get_pcm_output_bit_depth(p_audio_codec);
	}
	return 16;
}

static int get_alac_bit_depth_hint() {
	const int requested = get_int_setting("editor/movie_writer/apple/audio_bit_depth");
	return requested >= 24 ? 24 : 16;
}

static int get_legacy_video_bitrate(int p_width, int p_height, int p_fps, const String &p_video_codec) {
	const double quality = CLAMP(double(GLOBAL_GET("editor/movie_writer/video_quality")), 0.0, 1.0);
	const double bits_per_pixel = (p_video_codec == "hevc") ? Math::lerp(0.05, 0.20, quality) : Math::lerp(0.08, 0.28, quality);
	return int(MAX(1.0, double(p_width) * p_height * p_fps * bits_per_pixel));
}

} // namespace

struct MovieWriterApple::WriterData {
	struct PendingVideoFrame {
		PackedByteArray image_data;
		uint32_t frame_index = 0;
	};

	struct PendingAudioBlock {
		Vector<uint8_t> audio_bytes;
		uint32_t block_index = 0;
		uint64_t audio_frames_written = 0;
	};

	AVAssetWriter *__strong writer = nil;
	AVAssetWriterInput *__strong video_input = nil;
	AVAssetWriterInput *__strong audio_input = nil;
	AVAssetWriterInputPixelBufferAdaptor *__strong video_adaptor = nil;
	NSDictionary<NSString *, id> *__strong pixel_buffer_attributes = nil;
	CMFormatDescriptionRef audio_source_format = nullptr;
	dispatch_queue_t video_queue = nullptr;
	dispatch_queue_t audio_queue = nullptr;
	Mutex queue_mutex;
	List<PendingVideoFrame> pending_video_frames;
	List<PendingAudioBlock> pending_audio_blocks;

	String output_path;
	String worker_error;
	Size2i movie_size;
	uint32_t fps = 0;
	uint32_t mix_rate = 0;
	uint32_t audio_channels = 0;
	uint32_t audio_frames_per_block = 0;
	uint32_t submitted_frame_count = 0;
	uint32_t submitted_audio_block_count = 0;
	uint32_t video_frame_count = 0;
	uint32_t audio_block_count = 0;
	uint32_t direct_pixel_buffer_allocations = 0;
	uint64_t submitted_audio_frames = 0;
	uint64_t audio_frames_written = 0;
	int audio_source_bit_depth = 16;
	bool preserve_alpha = false;
	bool debug_logging = false;
	bool end_requested = false;
	bool worker_failed = false;
	bool video_input_finished = false;
	bool audio_input_finished = false;
	uint64_t video_wait_start_msec = 0;
	uint64_t audio_wait_start_msec = 0;
	uint32_t video_drain_invocations = 0;
	uint32_t audio_drain_invocations = 0;
	uint32_t max_pending_video_frames = 0;
	uint32_t max_pending_audio_blocks = 0;

	~WriterData() {
		if (audio_source_format != nullptr) {
			CFRelease(audio_source_format);
		}
	}
};

static void set_worker_failure(MovieWriterApple::WriterData *p_data, const String &p_error);

static void set_worker_failure(MovieWriterApple::WriterData *p_data, const String &p_error) {
	{
		MutexLock lock(p_data->queue_mutex);
		if (p_data->worker_failed) {
			return;
		}
		p_data->worker_failed = true;
		p_data->worker_error = p_error;
	}
	debug_log(p_data->debug_logging, "Worker failure: " + p_error);
}

static bool is_worker_failed(MovieWriterApple::WriterData *p_data, String *r_error = nullptr) {
	MutexLock lock(p_data->queue_mutex);
	if (r_error != nullptr) {
		*r_error = p_data->worker_error;
	}
	return p_data->worker_failed;
}

static void dispatch_drain_queues(MovieWriterApple::WriterData *p_data);

static Error encode_video_frame(MovieWriterApple::WriterData *p_data, const MovieWriterApple::WriterData::PendingVideoFrame &p_frame) {
	ERR_FAIL_COND_V_MSG(p_frame.image_data.size() < size_t(p_data->movie_size.width) * size_t(p_data->movie_size.height) * 4, ERR_INVALID_DATA, vformat("MovieWriterApple: queued frame %d data is truncated.", p_frame.frame_index));
	ERR_FAIL_COND_V_MSG(p_data->writer.status != AVAssetWriterStatusWriting, FAILED, "MovieWriterApple: AVAssetWriter is no longer writable: " + ns_error_to_string(p_data->writer.error));

	const int width = p_data->movie_size.width;
	const int height = p_data->movie_size.height;

	CVPixelBufferRef pixel_buffer = nullptr;
	CVReturn pixel_buffer_result = kCVReturnError;
	CVPixelBufferPoolRef pixel_buffer_pool = p_data->video_adaptor.pixelBufferPool;
	if (pixel_buffer_pool != nullptr) {
		pixel_buffer_result = CVPixelBufferPoolCreatePixelBuffer(nullptr, pixel_buffer_pool, &pixel_buffer);
	}
	if (pixel_buffer == nullptr) {
		pixel_buffer_result = CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)p_data->pixel_buffer_attributes, &pixel_buffer);
		if (pixel_buffer != nullptr) {
			p_data->direct_pixel_buffer_allocations++;
			debug_log(p_data->debug_logging, vformat("Allocated fallback pixel buffer directly at frame %d (count=%d).", p_frame.frame_index, p_data->direct_pixel_buffer_allocations));
		}
	}
	ERR_FAIL_COND_V_MSG(pixel_buffer_result != kCVReturnSuccess || pixel_buffer == nullptr, ERR_CANT_CREATE, vformat("MovieWriterApple: failed to allocate pixel buffer (%d).", int(pixel_buffer_result)));

	const uint8_t *src = p_frame.image_data.ptr();
	const bool preserve_alpha = p_data->preserve_alpha;

	const CVReturn lock_result = CVPixelBufferLockBaseAddress(pixel_buffer, 0);
	if (lock_result != kCVReturnSuccess) {
		CVPixelBufferRelease(pixel_buffer);
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("MovieWriterApple: failed to lock pixel buffer base address (%d).", int(lock_result)));
	}
	if (CVPixelBufferIsPlanar(pixel_buffer)) {
		CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
		CVPixelBufferRelease(pixel_buffer);
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "MovieWriterApple: unexpected planar pixel buffer returned for BGRA video encoding.");
	}
	uint8_t *dst_base = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(pixel_buffer));
	if (dst_base == nullptr) {
		CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
		CVPixelBufferRelease(pixel_buffer);
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, "MovieWriterApple: pixel buffer has no writable base address.");
	}
	const size_t dst_stride = CVPixelBufferGetBytesPerRow(pixel_buffer);
	if (dst_stride < size_t(width) * 4) {
		CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
		CVPixelBufferRelease(pixel_buffer);
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("MovieWriterApple: pixel buffer stride %d is too small for width %d.", int(dst_stride), width));
	}
	for (int y = 0; y < height; y++) {
		const uint8_t *src_row = src + (y * width * 4);
		uint8_t *dst_row = dst_base + (y * dst_stride);
		for (int x = 0; x < width; x++) {
			const int src_ofs = x * 4;
			const int dst_ofs = x * 4;
			dst_row[dst_ofs + 0] = src_row[src_ofs + 2];
			dst_row[dst_ofs + 1] = src_row[src_ofs + 1];
			dst_row[dst_ofs + 2] = src_row[src_ofs + 0];
			dst_row[dst_ofs + 3] = preserve_alpha ? src_row[src_ofs + 3] : 255;
		}
	}
	CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);

	const CMTime video_pts = CMTimeMake(p_frame.frame_index, p_data->fps);
	const bool appended_video = [p_data->video_adaptor appendPixelBuffer:pixel_buffer withPresentationTime:video_pts];
	CVPixelBufferRelease(pixel_buffer);
	if (!appended_video) {
		debug_log(p_data->debug_logging, vformat("Video append failed at frame %d (pts=%d/%d, writer status=%s, error=%s).", p_frame.frame_index, video_pts.value, video_pts.timescale, writer_status_to_string(p_data->writer.status), ns_error_to_string(p_data->writer.error)));
	}
	ERR_FAIL_COND_V_MSG(!appended_video, FAILED, "MovieWriterApple: failed to append video frame: " + ns_error_to_string(p_data->writer.error));

	if (p_data->debug_logging && ((p_frame.frame_index % 60) == 0 || p_frame.frame_index < 5)) {
		debug_log(true, vformat("Appended frame %d. Current file size: %s.", p_frame.frame_index, String::humanize_size(FileAccess::get_size(p_data->output_path))));
	}
	{
		MutexLock lock(p_data->queue_mutex);
		p_data->video_frame_count = p_frame.frame_index + 1;
	}

	return OK;
}

static Error encode_audio_block(MovieWriterApple::WriterData *p_data, const MovieWriterApple::WriterData::PendingAudioBlock &p_block) {
	ERR_FAIL_COND_V_MSG(p_data->writer.status != AVAssetWriterStatusWriting, FAILED, "MovieWriterApple: AVAssetWriter is no longer writable: " + ns_error_to_string(p_data->writer.error));

	CMBlockBufferRef block_buffer = nullptr;
	const size_t audio_data_size = p_block.audio_bytes.size();
	OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, audio_data_size, kCFAllocatorDefault, nullptr, 0, audio_data_size, kCMBlockBufferAssureMemoryNowFlag, &block_buffer);
	ERR_FAIL_COND_V_MSG(status != noErr || block_buffer == nullptr, ERR_CANT_CREATE, vformat("MovieWriterApple: failed to create audio block buffer (%d).", int(status)));

	status = CMBlockBufferReplaceDataBytes(p_block.audio_bytes.ptr(), block_buffer, 0, audio_data_size);
	if (status != noErr) {
		CFRelease(block_buffer);
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("MovieWriterApple: failed to fill audio block buffer (%d).", int(status)));
	}

	CMSampleBufferRef sample_buffer = nullptr;
	const CMTime audio_pts = CMTimeMake(p_block.audio_frames_written, p_data->mix_rate);
	status = CMAudioSampleBufferCreateReadyWithPacketDescriptions(kCFAllocatorDefault, block_buffer, p_data->audio_source_format, p_data->audio_frames_per_block, audio_pts, nullptr, &sample_buffer);
	if (status != noErr || sample_buffer == nullptr) {
		CFRelease(block_buffer);
		ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("MovieWriterApple: failed to create audio sample buffer (%d).", int(status)));
	}

	const bool appended_audio = [p_data->audio_input appendSampleBuffer:sample_buffer];
	CFRelease(sample_buffer);
	CFRelease(block_buffer);
	if (!appended_audio) {
		debug_log(p_data->debug_logging, vformat("Audio append failed at block %d (pts=%d/%d, samples=%d, writer status=%s, error=%s).", p_block.block_index, audio_pts.value, audio_pts.timescale, p_data->audio_frames_per_block, writer_status_to_string(p_data->writer.status), ns_error_to_string(p_data->writer.error)));
	}
	ERR_FAIL_COND_V_MSG(!appended_audio, FAILED, "MovieWriterApple: failed to append audio frame block: " + ns_error_to_string(p_data->writer.error));

	{
		MutexLock lock(p_data->queue_mutex);
		p_data->audio_block_count = p_block.block_index + 1;
		p_data->audio_frames_written = p_block.audio_frames_written + p_data->audio_frames_per_block;
	}
	if (p_data->debug_logging && (p_block.block_index < 5 || (p_block.block_index % 60) == 0)) {
		debug_log(true, vformat("Appended audio block %d. Current file size: %s.", p_block.block_index, String::humanize_size(FileAccess::get_size(p_data->output_path))));
	}

	return OK;
}

static void log_input_backpressure(MovieWriterApple::WriterData *p_data, AVAssetWriterInput *p_input, uint64_t &r_wait_start_msec, const char *p_label, uint32_t p_index) {
	static constexpr uint64_t INPUT_READY_TIMEOUT_MSEC = 30000;
	if ([p_input isReadyForMoreMediaData]) {
		if (r_wait_start_msec != 0) {
			const uint64_t waited_msec = Time::get_singleton()->get_ticks_msec() - r_wait_start_msec;
			debug_log(p_data->debug_logging, vformat("%s input became ready after %d ms at frame %d.", p_label, waited_msec, p_index));
			r_wait_start_msec = 0;
		}
		return;
	}

	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (r_wait_start_msec == 0) {
		r_wait_start_msec = now;
		debug_log(p_data->debug_logging, vformat("Waiting for %s input readiness at frame %d.", p_label, p_index));
		return;
	}

	const uint64_t waited_msec = now - r_wait_start_msec;
	if (p_data->debug_logging && (waited_msec / 1000) != ((waited_msec - 1) / 1000)) {
		debug_log(true, vformat("%s input still not ready after %d ms at frame %d (writer status: `%s`).", p_label, waited_msec, p_index, writer_status_to_string(p_data->writer.status)));
	}
	if (waited_msec >= INPUT_READY_TIMEOUT_MSEC) {
		set_worker_failure(p_data, vformat("%s input timed out after %d ms at frame %d (writer status: `%s`, error: %s).", p_label, waited_msec, p_index, writer_status_to_string(p_data->writer.status), ns_error_to_string(p_data->writer.error)));
	}
}

static void drain_video_queue(MovieWriterApple::WriterData *p_data) {
	@autoreleasepool {
		if (is_worker_failed(p_data)) {
			return;
		}
		p_data->video_drain_invocations++;
		if (p_data->debug_logging && (p_data->video_drain_invocations <= 5 || (p_data->video_drain_invocations % 60) == 0)) {
			uint32_t submitted_frame_count = 0;
			uint32_t video_frame_count = 0;
			{
				MutexLock lock(p_data->queue_mutex);
				submitted_frame_count = p_data->submitted_frame_count;
				video_frame_count = p_data->video_frame_count;
			}
			debug_log(true, vformat("Video drain invocation %d (submitted=%d, appended=%d).", p_data->video_drain_invocations, submitted_frame_count, video_frame_count));
		}
		if (![p_data->video_input isReadyForMoreMediaData]) {
			uint32_t video_frame_count = 0;
			{
				MutexLock lock(p_data->queue_mutex);
				video_frame_count = p_data->video_frame_count;
			}
			log_input_backpressure(p_data, p_data->video_input, p_data->video_wait_start_msec, "video", video_frame_count);
			return;
		}

		while ([p_data->video_input isReadyForMoreMediaData]) {
			MovieWriterApple::WriterData::PendingVideoFrame frame;
			bool has_frame = false;
			bool should_finish = false;
			{
				MutexLock lock(p_data->queue_mutex);
				if (!p_data->pending_video_frames.is_empty()) {
					frame = p_data->pending_video_frames.front()->get();
					p_data->pending_video_frames.pop_front();
					has_frame = true;
				} else if (p_data->end_requested && !p_data->video_input_finished) {
					p_data->video_input_finished = true;
					should_finish = true;
				}
			}

			if (has_frame) {
				p_data->video_wait_start_msec = 0;
				const Error err = encode_video_frame(p_data, frame);
				if (err != OK) {
					set_worker_failure(p_data, vformat("Video encoding failed on frame %d with error code %d.", frame.frame_index, err));
					return;
				}
				continue;
			}

			if (should_finish) {
				uint32_t video_frame_count = 0;
				{
					MutexLock lock(p_data->queue_mutex);
					video_frame_count = p_data->video_frame_count;
				}
				debug_log(p_data->debug_logging, vformat("Marking video input finished after %d frames.", video_frame_count));
				[p_data->video_input markAsFinished];
			}
			return;
		}
	}
}

static void drain_audio_queue(MovieWriterApple::WriterData *p_data) {
	@autoreleasepool {
		if (is_worker_failed(p_data)) {
			return;
		}
		p_data->audio_drain_invocations++;
		if (p_data->debug_logging && (p_data->audio_drain_invocations <= 5 || (p_data->audio_drain_invocations % 60) == 0)) {
			uint32_t submitted_audio_block_count = 0;
			uint32_t audio_block_count = 0;
			{
				MutexLock lock(p_data->queue_mutex);
				submitted_audio_block_count = p_data->submitted_audio_block_count;
				audio_block_count = p_data->audio_block_count;
			}
			debug_log(true, vformat("Audio drain invocation %d (submitted=%d, appended=%d).", p_data->audio_drain_invocations, submitted_audio_block_count, audio_block_count));
		}
		if (![p_data->audio_input isReadyForMoreMediaData]) {
			uint32_t audio_block_count = 0;
			{
				MutexLock lock(p_data->queue_mutex);
				audio_block_count = p_data->audio_block_count;
			}
			log_input_backpressure(p_data, p_data->audio_input, p_data->audio_wait_start_msec, "audio", audio_block_count);
			return;
		}

		while ([p_data->audio_input isReadyForMoreMediaData]) {
			MovieWriterApple::WriterData::PendingAudioBlock block;
			bool has_block = false;
			bool should_finish = false;
			{
				MutexLock lock(p_data->queue_mutex);
				if (!p_data->pending_audio_blocks.is_empty()) {
					block = p_data->pending_audio_blocks.front()->get();
					p_data->pending_audio_blocks.pop_front();
					has_block = true;
				} else if (p_data->end_requested && !p_data->audio_input_finished) {
					p_data->audio_input_finished = true;
					should_finish = true;
				}
			}

			if (has_block) {
				p_data->audio_wait_start_msec = 0;
				const Error err = encode_audio_block(p_data, block);
				if (err != OK) {
					set_worker_failure(p_data, vformat("Audio encoding failed on block %d with error code %d.", block.block_index, err));
					return;
				}
				continue;
			}

			if (should_finish) {
				uint32_t audio_block_count = 0;
				{
					MutexLock lock(p_data->queue_mutex);
					audio_block_count = p_data->audio_block_count;
				}
				debug_log(p_data->debug_logging, vformat("Marking audio input finished after %d audio blocks.", audio_block_count));
				[p_data->audio_input markAsFinished];
			}
			return;
		}
	}
}

static void dispatch_drain_queues(MovieWriterApple::WriterData *p_data) {
	dispatch_async(p_data->video_queue, ^{
		drain_video_queue(p_data);
	});
	dispatch_async(p_data->audio_queue, ^{
		drain_audio_queue(p_data);
	});
}

static Error wait_for_queue_space(MovieWriterApple::WriterData *p_data) {
	uint64_t wait_start_msec = 0;

	while (true) {
		String worker_error;
		bool worker_failed = false;
		bool end_requested = false;
		bool has_space = false;
		uint32_t pending_video_frame_count = 0;
		uint32_t pending_audio_block_count = 0;
		{
			MutexLock lock(p_data->queue_mutex);
			worker_failed = p_data->worker_failed;
			worker_error = p_data->worker_error;
			end_requested = p_data->end_requested;
			pending_video_frame_count = p_data->pending_video_frames.size();
			pending_audio_block_count = p_data->pending_audio_blocks.size();
			has_space = pending_video_frame_count < MAX_PENDING_VIDEO_FRAMES && pending_audio_block_count < MAX_PENDING_AUDIO_BLOCKS;
		}

		ERR_FAIL_COND_V_MSG(worker_failed, FAILED, "MovieWriterApple: encoding worker failed: " + worker_error);
		ERR_FAIL_COND_V_MSG(end_requested, ERR_BUSY, "MovieWriterApple: cannot queue frames after recording has been stopped.");
		if (has_space) {
			return OK;
		}

		dispatch_drain_queues(p_data);

		const uint64_t now = Time::get_singleton()->get_ticks_msec();
		if (wait_start_msec == 0) {
			wait_start_msec = now;
			debug_log(p_data->debug_logging, vformat("Waiting for encoder queue space (video queue=%d audio queue=%d).", pending_video_frame_count, pending_audio_block_count));
		} else if ((now - wait_start_msec) >= QUEUE_WAIT_TIMEOUT_MSEC) {
			set_worker_failure(p_data, vformat("Timed out waiting for encoder queue space after %d ms.", now - wait_start_msec));
			ERR_FAIL_V_MSG(FAILED, "MovieWriterApple: timed out waiting for encoder queue space.");
		}

		OS::get_singleton()->delay_usec(QUEUE_WAIT_SLEEP_USEC);
	}
}

static Error wait_for_queues_to_finish(MovieWriterApple::WriterData *p_data) {
	uint64_t wait_start_msec = 0;

	while (true) {
		String worker_error;
		bool worker_failed = false;
		bool finished = false;
		{
			MutexLock lock(p_data->queue_mutex);
			worker_failed = p_data->worker_failed;
			worker_error = p_data->worker_error;
			finished = p_data->video_input_finished && p_data->audio_input_finished;
		}

		if (worker_failed) {
			ERR_FAIL_V_MSG(FAILED, "MovieWriterApple: encoding worker failed: " + worker_error);
		}
		if (finished) {
			return OK;
		}

		dispatch_drain_queues(p_data);

		const uint64_t now = Time::get_singleton()->get_ticks_msec();
		if (wait_start_msec == 0) {
			wait_start_msec = now;
			debug_log(p_data->debug_logging, "Waiting for encoder queues to drain before finishing.");
		} else if ((now - wait_start_msec) >= QUEUE_WAIT_TIMEOUT_MSEC) {
			set_worker_failure(p_data, vformat("Timed out waiting for encoder queues to finish after %d ms.", now - wait_start_msec));
			ERR_FAIL_V_MSG(FAILED, "MovieWriterApple: timed out waiting for encoder queues to finish.");
		}

		OS::get_singleton()->delay_usec(QUEUE_WAIT_SLEEP_USEC);
	}
}

MovieWriterApple::MovieWriterApple() {}

MovieWriterApple::~MovieWriterApple() {
	write_end();
}

uint32_t MovieWriterApple::get_audio_mix_rate() const {
	return uint32_t(get_int_setting("editor/movie_writer/mix_rate"));
}

AudioServer::SpeakerMode MovieWriterApple::get_audio_speaker_mode() const {
	return AudioServer::SpeakerMode(get_int_setting("editor/movie_writer/speaker_mode"));
}

void MovieWriterApple::get_supported_extensions(List<String> *r_extensions) const {
	r_extensions->push_back("mov");
	r_extensions->push_back("mp4");
	r_extensions->push_back("m4v");
}

bool MovieWriterApple::handles_file(const String &p_path) const {
	const String extension = p_path.get_extension().to_lower();
	return extension == "mov" || extension == "mp4" || extension == "m4v";
}

Error MovieWriterApple::write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) {
	write_end();

	@autoreleasepool {
		WriterData *new_data = memnew(WriterData);
		new_data->movie_size = p_movie_size;
		new_data->fps = p_fps;
		new_data->mix_rate = get_audio_mix_rate();
		new_data->audio_channels = get_channel_count(get_audio_speaker_mode());
		new_data->audio_frames_per_block = new_data->mix_rate / p_fps;

		String output_path = p_base_path;
		if (output_path.is_relative_path()) {
			output_path = "res://" + output_path;
		}
		output_path = ProjectSettings::get_singleton()->globalize_path(output_path);
		new_data->output_path = output_path;

		AVFileType file_type = get_output_file_type(output_path);
		if (file_type == nil) {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_FILE_UNRECOGNIZED, "Unsupported Apple movie writer extension: " + output_path.get_extension());
		}

		const String video_codec = get_string_setting("editor/movie_writer/apple/video_codec");
		const String audio_codec = get_string_setting("editor/movie_writer/apple/audio_codec");
		const String prores_profile = get_string_setting("editor/movie_writer/apple/prores/profile");
		const bool allow_alpha = get_bool_setting("editor/movie_writer/apple/allow_alpha");
		const int requested_video_bitrate = get_int_setting("editor/movie_writer/apple/video_bitrate");
		const int requested_audio_bitrate = get_int_setting("editor/movie_writer/apple/audio_bitrate");
		const int keyframe_interval = get_int_setting("editor/movie_writer/apple/keyframe_interval");
		const double video_quality = CLAMP(double(GLOBAL_GET("editor/movie_writer/video_quality")), 0.0, 1.0);
		new_data->debug_logging = get_bool_setting("editor/movie_writer/apple/debug_logging");

		AVVideoCodecType selected_video_codec = AVVideoCodecTypeHEVC;
		bool preserve_alpha = false;

		if (video_codec == "h264") {
			selected_video_codec = AVVideoCodecTypeH264;
			if (allow_alpha) {
				WARN_PRINT("MovieWriterApple: alpha output is not supported with H.264, ignoring `editor/movie_writer/apple/allow_alpha`.");
			}
		} else if (video_codec == "hevc") {
			if (allow_alpha) {
				if (@available(macOS 10.15, iOS 13.0, visionOS 1.0, *)) {
					selected_video_codec = AVVideoCodecTypeHEVCWithAlpha;
					preserve_alpha = true;
				} else {
					WARN_PRINT("MovieWriterApple: HEVC with alpha is not available on this OS version, falling back to opaque HEVC.");
					selected_video_codec = AVVideoCodecTypeHEVC;
				}
			} else {
				selected_video_codec = AVVideoCodecTypeHEVC;
			}
		} else if (video_codec == "prores") {
#if defined(VISIONOS_ENABLED)
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "MovieWriterApple: ProRes encoding is unavailable on visionOS.");
#else
			if (prores_profile == "proxy") {
				if (@available(macOS 10.15, iOS 13.0, *)) {
					selected_video_codec = AVVideoCodecTypeAppleProRes422Proxy;
				} else {
					WARN_PRINT("MovieWriterApple: ProRes Proxy is not available on this OS version, falling back to ProRes 422.");
					selected_video_codec = AVVideoCodecTypeAppleProRes422;
				}
			} else if (prores_profile == "lt") {
				if (@available(macOS 10.15, iOS 13.0, *)) {
					selected_video_codec = AVVideoCodecTypeAppleProRes422LT;
				} else {
					WARN_PRINT("MovieWriterApple: ProRes LT is not available on this OS version, falling back to ProRes 422.");
					selected_video_codec = AVVideoCodecTypeAppleProRes422;
				}
			} else if (prores_profile == "hq") {
				if (@available(macOS 10.15, iOS 13.0, *)) {
					selected_video_codec = AVVideoCodecTypeAppleProRes422HQ;
				} else {
					WARN_PRINT("MovieWriterApple: ProRes HQ is not available on this OS version, falling back to ProRes 422.");
					selected_video_codec = AVVideoCodecTypeAppleProRes422;
				}
			} else if (prores_profile == "4444") {
				selected_video_codec = AVVideoCodecTypeAppleProRes4444;
				preserve_alpha = allow_alpha;
			} else if (prores_profile == "4444_xq") {
				if (@available(macOS 15.0, iOS 18.0, *)) {
					selected_video_codec = AVVideoCodecTypeAppleProRes4444XQ;
				} else {
					WARN_PRINT("MovieWriterApple: ProRes 4444 XQ is not available on this OS version, falling back to ProRes 4444.");
					selected_video_codec = AVVideoCodecTypeAppleProRes4444;
				}
				preserve_alpha = allow_alpha;
			} else {
				selected_video_codec = AVVideoCodecTypeAppleProRes422;
				if (prores_profile != "422") {
					WARN_PRINT("MovieWriterApple: unknown ProRes profile, falling back to `422`.");
				}
			}

			if (allow_alpha && !(prores_profile == "4444" || prores_profile == "4444_xq")) {
				WARN_PRINT("MovieWriterApple: alpha output requires `editor/movie_writer/apple/prores/profile` to be `4444` or `4444_xq`, ignoring alpha.");
			}
#endif
		} else if (video_codec == "mjpeg") {
			selected_video_codec = AVVideoCodecTypeJPEG;
			if (allow_alpha) {
				WARN_PRINT("MovieWriterApple: alpha output is not supported with MJPEG, ignoring `editor/movie_writer/apple/allow_alpha`.");
			}
		} else {
			WARN_PRINT("MovieWriterApple: unknown video codec, falling back to HEVC.");
			selected_video_codec = AVVideoCodecTypeHEVC;
		}

		NSMutableDictionary<NSString *, id> *video_compression = [NSMutableDictionary dictionary];
		if (video_codec == "h264" || video_codec == "hevc") {
			const int video_bitrate = requested_video_bitrate > 0 ? requested_video_bitrate : get_legacy_video_bitrate(p_movie_size.width, p_movie_size.height, p_fps, video_codec);
			video_compression[(__bridge NSString *)kVTCompressionPropertyKey_AverageBitRate] = @(video_bitrate);
			video_compression[(__bridge NSString *)kVTCompressionPropertyKey_MaxKeyFrameInterval] = @(MAX(1, keyframe_interval));
			video_compression[(__bridge NSString *)kVTCompressionPropertyKey_ExpectedFrameRate] = @(p_fps);
		} else if (video_codec == "mjpeg") {
			video_compression[AVVideoQualityKey] = @(video_quality);
		}

		NSMutableDictionary<NSString *, id> *video_settings = [@{
			AVVideoCodecKey : selected_video_codec,
			AVVideoWidthKey : @(p_movie_size.width),
			AVVideoHeightKey : @(p_movie_size.height),
		} mutableCopy];
		if ([video_compression count] > 0) {
			video_settings[AVVideoCompressionPropertiesKey] = video_compression;
		}

		NSMutableDictionary<NSString *, id> *audio_settings = [@{
			AVFormatIDKey : @(kAudioFormatMPEG4AAC),
			AVSampleRateKey : @(double(new_data->mix_rate)),
			AVNumberOfChannelsKey : @(new_data->audio_channels),
		} mutableCopy];
		if (new_data->audio_channels > 2) {
			audio_settings[AVChannelLayoutKey] = create_channel_layout_data(get_audio_speaker_mode());
		}

		if (audio_codec == "aac") {
			audio_settings[AVFormatIDKey] = @(kAudioFormatMPEG4AAC);
			audio_settings[AVEncoderBitRateKey] = @(MAX(8000, requested_audio_bitrate));
		} else if (audio_codec == "pcm_s16le" || audio_codec == "pcm_s24le") {
			const int pcm_bit_depth = get_pcm_output_bit_depth(audio_codec);
			audio_settings[AVFormatIDKey] = @(kAudioFormatLinearPCM);
			audio_settings[AVLinearPCMBitDepthKey] = @(pcm_bit_depth);
			audio_settings[AVLinearPCMIsBigEndianKey] = @NO;
			audio_settings[AVLinearPCMIsFloatKey] = @NO;
			audio_settings[AVLinearPCMIsNonInterleavedKey] = @NO;
		} else if (audio_codec == "alac") {
			audio_settings[AVFormatIDKey] = @(kAudioFormatAppleLossless);
			audio_settings[AVEncoderBitDepthHintKey] = @(get_alac_bit_depth_hint());
		} else {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "MovieWriterApple: unsupported audio codec `" + audio_codec + "`.");
		}

		new_data->audio_source_bit_depth = get_audio_source_bit_depth(audio_codec);
		new_data->preserve_alpha = preserve_alpha;

		AudioStreamBasicDescription asbd = {};
		asbd.mSampleRate = new_data->mix_rate;
		asbd.mFormatID = kAudioFormatLinearPCM;
		asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
		asbd.mFramesPerPacket = 1;
		asbd.mChannelsPerFrame = new_data->audio_channels;
		asbd.mBitsPerChannel = new_data->audio_source_bit_depth;
		asbd.mBytesPerFrame = (new_data->audio_channels * new_data->audio_source_bit_depth) / 8;
		asbd.mBytesPerPacket = asbd.mBytesPerFrame;

		const AudioChannelLayout layout = create_channel_layout(get_audio_speaker_mode());
		const OSStatus format_error = CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, sizeof(layout), &layout, 0, nullptr, nullptr, &new_data->audio_source_format);
		if (format_error != noErr) {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, vformat("MovieWriterApple: failed to create audio source format description (%d).", int(format_error)));
		}

		CharString output_utf8 = output_path.utf8();
		NSString *ns_output_path = [NSString stringWithUTF8String:output_utf8.get_data()];
		NSURL *output_url = [NSURL fileURLWithPath:ns_output_path];
		[[NSFileManager defaultManager] removeItemAtURL:output_url error:nil];
		NSError *error = nil;
		new_data->writer = [[AVAssetWriter alloc] initWithURL:output_url fileType:file_type error:&error];
		if (new_data->writer != nil) {
			new_data->writer.shouldOptimizeForNetworkUse = YES;
		}
		if (new_data->writer == nil) {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, "MovieWriterApple: failed to create AVAssetWriter: " + ns_error_to_string(error));
		}

		if (![new_data->writer canApplyOutputSettings:video_settings forMediaType:AVMediaTypeVideo]) {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "MovieWriterApple: the selected video codec/container/settings combination is not supported by AVFoundation.");
		}
		if (![new_data->writer canApplyOutputSettings:audio_settings forMediaType:AVMediaTypeAudio]) {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "MovieWriterApple: the selected audio codec/container/settings combination is not supported by AVFoundation.");
		}

		new_data->video_input = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo outputSettings:video_settings];
		new_data->video_input.expectsMediaDataInRealTime = NO;

		new_data->pixel_buffer_attributes = @{
			(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
			(id)kCVPixelBufferWidthKey : @(p_movie_size.width),
			(id)kCVPixelBufferHeightKey : @(p_movie_size.height),
			(id)kCVPixelBufferCGImageCompatibilityKey : @YES,
			(id)kCVPixelBufferCGBitmapContextCompatibilityKey : @YES,
			(id)kCVPixelBufferIOSurfacePropertiesKey : @{},
		};
		new_data->video_adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc] initWithAssetWriterInput:new_data->video_input sourcePixelBufferAttributes:new_data->pixel_buffer_attributes];

		new_data->audio_input = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio outputSettings:audio_settings sourceFormatHint:new_data->audio_source_format];
		new_data->audio_input.expectsMediaDataInRealTime = NO;

		if (![new_data->writer canAddInput:new_data->video_input]) {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, "MovieWriterApple: failed to add AVFoundation video input.");
		}
		if (![new_data->writer canAddInput:new_data->audio_input]) {
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, "MovieWriterApple: failed to add AVFoundation audio input.");
		}
		[new_data->writer addInput:new_data->video_input];
		[new_data->writer addInput:new_data->audio_input];

		debug_log(new_data->debug_logging, vformat("Starting writer for `%s` (%s).", output_path, ns_string_to_string(file_type)));
		debug_log(new_data->debug_logging, vformat("Video: codec=%s size=%dx%d fps=%d alpha=%s bitrate=%d keyframe_interval=%d.", ns_string_to_string(selected_video_codec), p_movie_size.width, p_movie_size.height, p_fps, preserve_alpha ? "yes" : "no", requested_video_bitrate, keyframe_interval));
		debug_log(new_data->debug_logging, vformat("Audio: codec=%s mix_rate=%d channels=%d block_frames=%d bitrate=%d source_bit_depth=%d.", audio_codec, new_data->mix_rate, new_data->audio_channels, new_data->audio_frames_per_block, requested_audio_bitrate, new_data->audio_source_bit_depth));

		if (![new_data->writer startWriting]) {
			const String error_text = ns_error_to_string(new_data->writer.error);
			memdelete(new_data);
			ERR_FAIL_V_MSG(ERR_CANT_CREATE, "MovieWriterApple: startWriting failed: " + error_text);
		}
		[new_data->writer startSessionAtSourceTime:kCMTimeZero];
		debug_log(new_data->debug_logging, "Writer session started at source time zero.");
		new_data->video_queue = dispatch_queue_create("org.godot.movie_writer_apple.video", DISPATCH_QUEUE_SERIAL);
		new_data->audio_queue = dispatch_queue_create("org.godot.movie_writer_apple.audio", DISPATCH_QUEUE_SERIAL);

		__block WriterData *block_data = new_data;
		[new_data->video_input requestMediaDataWhenReadyOnQueue:new_data->video_queue
													 usingBlock:^{
														 drain_video_queue(block_data);
													 }];

		[new_data->audio_input requestMediaDataWhenReadyOnQueue:new_data->audio_queue
													 usingBlock:^{
														 drain_audio_queue(block_data);
													 }];

		data = new_data;
	}

	return OK;
}

Error MovieWriterApple::write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) {
	ERR_FAIL_COND_V(data == nullptr, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(p_image.is_null(), ERR_INVALID_DATA);
	ERR_FAIL_NULL_V(p_audio_data, ERR_INVALID_DATA);

	{
		MutexLock lock(data->queue_mutex);
		ERR_FAIL_COND_V_MSG(data->worker_failed, FAILED, "MovieWriterApple: encoding worker failed: " + data->worker_error);
	}
	const Error queue_space_error = wait_for_queue_space(data);
	ERR_FAIL_COND_V(queue_space_error != OK, queue_space_error);

	Ref<Image> image = p_image;
	if (image->get_format() != Image::FORMAT_RGBA8) {
		image = image->duplicate();
		ERR_FAIL_COND_V_MSG(image.is_null(), ERR_CANT_CREATE, "MovieWriterApple: failed to duplicate the source frame image.");
		image->convert(Image::FORMAT_RGBA8);
	}

	const int width = image->get_width();
	const int height = image->get_height();
	ERR_FAIL_COND_V_MSG(width != data->movie_size.width || height != data->movie_size.height, ERR_INVALID_DATA, vformat("MovieWriterApple: frame size %dx%d does not match configured movie size %dx%d.", width, height, data->movie_size.width, data->movie_size.height));

	WriterData::PendingVideoFrame video_frame;
	video_frame.image_data = image->get_data();
	const size_t required_image_bytes = size_t(width) * size_t(height) * 4;
	ERR_FAIL_COND_V_MSG(video_frame.image_data.size() < required_image_bytes, ERR_INVALID_DATA, vformat("MovieWriterApple: frame data is truncated (%d bytes for %dx%d RGBA8 image).", video_frame.image_data.size(), width, height));

	WriterData::PendingAudioBlock audio_block;
	const int total_samples = int(data->audio_frames_per_block * data->audio_channels);
	if (data->audio_source_bit_depth == 24) {
		audio_block.audio_bytes.resize(total_samples * 3);
		uint8_t *dst = audio_block.audio_bytes.ptrw();
		for (int i = 0; i < total_samples; i++) {
			const int32_t sample = p_audio_data[i] >> 8;
			dst[(i * 3) + 0] = uint8_t(sample & 0xFF);
			dst[(i * 3) + 1] = uint8_t((sample >> 8) & 0xFF);
			dst[(i * 3) + 2] = uint8_t((sample >> 16) & 0xFF);
		}
	} else {
		audio_block.audio_bytes.resize(total_samples * 2);
		int16_t *dst = reinterpret_cast<int16_t *>(audio_block.audio_bytes.ptrw());
		for (int i = 0; i < total_samples; i++) {
			dst[i] = int16_t(p_audio_data[i] >> 16);
		}
	}

	{
		MutexLock lock(data->queue_mutex);
		if (data->worker_failed) {
			ERR_FAIL_V_MSG(FAILED, "MovieWriterApple: encoding worker failed: " + data->worker_error);
		}
		if (data->end_requested) {
			ERR_FAIL_V_MSG(ERR_BUSY, "MovieWriterApple: cannot queue frames after recording has been stopped.");
		}
		video_frame.frame_index = data->submitted_frame_count;
		audio_block.block_index = data->submitted_audio_block_count;
		audio_block.audio_frames_written = data->submitted_audio_frames;
		data->pending_video_frames.push_back(video_frame);
		data->pending_audio_blocks.push_back(audio_block);
		data->max_pending_video_frames = MAX<uint32_t>(data->max_pending_video_frames, data->pending_video_frames.size());
		data->max_pending_audio_blocks = MAX<uint32_t>(data->max_pending_audio_blocks, data->pending_audio_blocks.size());
		if (data->debug_logging && (video_frame.frame_index < 5 || (video_frame.frame_index % 60) == 0)) {
			debug_log(true, vformat("Queued frame %d and audio block %d (video queue=%d, audio queue=%d).", video_frame.frame_index, audio_block.block_index, data->pending_video_frames.size(), data->pending_audio_blocks.size()));
		}
		if (data->debug_logging && (data->pending_video_frames.size() == 120 || data->pending_audio_blocks.size() == 120)) {
			debug_log(true, vformat("Queue backlog reached video=%d audio=%d (submitted video=%d appended video=%d submitted audio=%d appended audio=%d).", data->pending_video_frames.size(), data->pending_audio_blocks.size(), data->submitted_frame_count, data->video_frame_count, data->submitted_audio_block_count, data->audio_block_count));
		}
		data->submitted_frame_count++;
		data->submitted_audio_block_count++;
		data->submitted_audio_frames += data->audio_frames_per_block;
	}
	dispatch_drain_queues(data);

	return OK;
}

void MovieWriterApple::write_end() {
	if (data == nullptr) {
		return;
	}

	@autoreleasepool {
		{
			MutexLock lock(data->queue_mutex);
			data->end_requested = true;
		}
		const Error drain_error = wait_for_queues_to_finish(data);

		String worker_error;
		const bool worker_failed = is_worker_failed(data, &worker_error) || drain_error != OK;
		if (worker_failed) {
			ERR_PRINT("MovieWriterApple: encoding worker failed: " + worker_error);
		}

		// The drain blocks capture `data` as a raw pointer, and AVFoundation keeps invoking them
		// while the writer is recording and their input has not been marked finished. Stop them
		// before `data` is freed: on the failure path the blocks bail out early, so they never
		// mark the inputs finished themselves.
		if (data->writer.status == AVAssetWriterStatusWriting) {
			bool finish_video = false;
			bool finish_audio = false;
			{
				MutexLock lock(data->queue_mutex);
				finish_video = !data->video_input_finished;
				finish_audio = !data->audio_input_finished;
				data->video_input_finished = true;
				data->audio_input_finished = true;
			}
			if (finish_video) {
				[data->video_input markAsFinished];
			}
			if (finish_audio) {
				[data->audio_input markAsFinished];
			}

			if (worker_failed) {
				[data->writer cancelWriting];
			}
		}

		// Flush any drain block that is already in flight, so none of them outlive `data`.
		dispatch_sync(data->video_queue, ^{
					  });
		dispatch_sync(data->audio_queue, ^{
					  });
		debug_log(data->debug_logging, vformat("Final queue stats: submitted video=%d appended video=%d submitted audio=%d appended audio=%d max video backlog=%d max audio backlog=%d.", data->submitted_frame_count, data->video_frame_count, data->submitted_audio_block_count, data->audio_block_count, data->max_pending_video_frames, data->max_pending_audio_blocks));

		if (!worker_failed && data->writer.status == AVAssetWriterStatusWriting) {
			debug_log(data->debug_logging, vformat("Finishing writer after %d video frames and %d audio blocks. Current file size: %s.", data->video_frame_count, data->audio_block_count, String::humanize_size(FileAccess::get_size(data->output_path))));

			dispatch_semaphore_t sem = dispatch_semaphore_create(0);
			[data->writer finishWritingWithCompletionHandler:^{
				dispatch_semaphore_signal(sem);
			}];
			dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

			debug_log(data->debug_logging, vformat("finishWriting completed with status `%s`. Final file size: %s.", writer_status_to_string(data->writer.status), String::humanize_size(FileAccess::get_size(data->output_path))));
			if (data->writer.status != AVAssetWriterStatusCompleted) {
				ERR_PRINT("MovieWriterApple: finishWriting failed: " + ns_error_to_string(data->writer.error));
			}
		} else {
			debug_log(data->debug_logging, vformat("write_end called while writer status is `%s` (error: %s). File size: %s.", writer_status_to_string(data->writer.status), ns_error_to_string(data->writer.error), String::humanize_size(FileAccess::get_size(data->output_path))));
		}
	}

	memdelete(data);
	data = nullptr;
}
