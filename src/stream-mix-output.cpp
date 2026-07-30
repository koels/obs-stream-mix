/*
 * Stream Mix - plugin-owned streaming output implementation.
 */
#include "stream-mix-output.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <string>

#define blog(level, format, ...) \
	blog(level, "[stream-mix][output] " format, ##__VA_ARGS__)

/* Map an OBS "StreamEncoder" profile value to an encoder id. */
static std::string map_video_encoder(const char *v)
{
	std::string s = v ? v : "";
	if (s.empty() || s == "x264" || s == "x264_lowcpu")
		return "obs_x264";
	if (s == "nvenc" || s == "jim_nvenc")
		return "jim_nvenc";
	if (s == "nvenc_hevc" || s == "jim_hevc_nvenc")
		return "jim_hevc_nvenc";
	if (s == "qsv" || s == "obs_qsv11" || s == "obs_qsv11_v2")
		return "obs_qsv11_v2";
	if (s == "amd" || s == "h264_texture_amf")
		return "h264_texture_amf";
	if (s == "apple" || s == "com.apple.videotoolbox.videoencoder.ave.avc")
		return "com.apple.videotoolbox.videoencoder.ave.avc";
	/* Assume it is already a valid encoder id. */
	return s;
}

obs_encoder_t *StreamMixOutput::create_video_encoder(const StreamMixConfig &cfg)
{
	config_t *pc = obs_frontend_get_profile_config();

	std::string enc_id = cfg.video_encoder_id;
	int bitrate = cfg.video_bitrate;

	if (pc) {
		if (enc_id.empty())
			enc_id = map_video_encoder(config_get_string(
				pc, "SimpleOutput", "StreamEncoder"));
		if (bitrate <= 0)
			bitrate = (int)config_get_uint(pc, "SimpleOutput",
						       "VBitrate");
	}
	if (enc_id.empty())
		enc_id = "obs_x264";
	if (bitrate <= 0)
		bitrate = 6000;

	obs_data_t *s = obs_data_create();
	obs_data_set_int(s, "bitrate", bitrate);
	obs_data_set_string(s, "rate_control", "CBR");
	obs_data_set_int(s, "keyint_sec", cfg.keyint_sec);
	obs_data_set_bool(s, "use_bufsize", true);

	obs_encoder_t *e = obs_video_encoder_create(
		enc_id.c_str(), "stream_mix_venc", s, nullptr);
	obs_data_release(s);

	if (!e) {
		/* Fall back to x264 if the profile's encoder is unavailable. */
		obs_data_t *s2 = obs_data_create();
		obs_data_set_int(s2, "bitrate", bitrate);
		obs_data_set_string(s2, "rate_control", "CBR");
		obs_data_set_int(s2, "keyint_sec", cfg.keyint_sec);
		e = obs_video_encoder_create("obs_x264", "stream_mix_venc",
					     s2, nullptr);
		obs_data_release(s2);
	}
	if (e)
		obs_encoder_set_video(e, obs_get_video());
	return e;
}

obs_encoder_t *StreamMixOutput::create_audio_encoder(const StreamMixConfig &cfg)
{
	config_t *pc = obs_frontend_get_profile_config();

	std::string enc_id = cfg.audio_encoder_id;
	int bitrate = cfg.audio_bitrate;
	if (pc && bitrate <= 0)
		bitrate = (int)config_get_uint(pc, "SimpleOutput", "ABitrate");
	if (enc_id.empty())
		enc_id = "ffmpeg_aac";
	if (bitrate <= 0)
		bitrate = 160;

	obs_data_t *s = obs_data_create();
	obs_data_set_int(s, "bitrate", bitrate);

	/* mixer_idx 0: our private audio_output only ever fills mix 0. */
	obs_encoder_t *e = obs_audio_encoder_create(
		enc_id.c_str(), "stream_mix_aenc", s, 0, nullptr);
	obs_data_release(s);

	if (e)
		obs_encoder_set_audio(e, audioEngine->handle());
	return e;
}

bool StreamMixOutput::active() const
{
	return output && obs_output_active(output);
}

bool StreamMixOutput::start(const StreamMixConfig &cfg)
{
	if (active())
		return true;

	/* 1. Bring up the private mix (opens audio_output, taps sources). */
	if (!audioEngine->start()) {
		blog(LOG_ERROR, "failed to start private audio mix");
		return false;
	}

	/* 2. Reuse the service configured in Settings -> Stream. Borrowed
	 * reference; do not release. */
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service) {
		blog(LOG_ERROR,
		     "no streaming service configured (Settings -> Stream)");
		audioEngine->stop();
		return false;
	}

	/* 3. Build our own output + encoders. */
	output = obs_output_create("rtmp_output", "stream_mix_output", nullptr,
				   nullptr);
	if (!output) {
		blog(LOG_ERROR, "obs_output_create(rtmp_output) failed");
		audioEngine->stop();
		return false;
	}
	obs_output_set_service(output, service);

	venc = create_video_encoder(cfg);
	aenc = create_audio_encoder(cfg);
	if (!venc || !aenc) {
		blog(LOG_ERROR, "encoder creation failed (v=%p a=%p)",
		     (void *)venc, (void *)aenc);
		stop();
		return false;
	}

	obs_output_set_video_encoder(output, venc);
	obs_output_set_audio_encoder(output, aenc, 0);

	if (!obs_output_start(output)) {
		const char *err = obs_output_get_last_error(output);
		blog(LOG_ERROR, "obs_output_start failed: %s",
		     err ? err : "(unknown)");
		stop();
		return false;
	}

	blog(LOG_INFO, "Stream Mix output started");
	return true;
}

void StreamMixOutput::stop()
{
	if (output) {
		if (obs_output_active(output))
			obs_output_stop(output);
		obs_output_release(output);
		output = nullptr;
	}
	if (venc) {
		obs_encoder_release(venc);
		venc = nullptr;
	}
	if (aenc) {
		obs_encoder_release(aenc);
		aenc = nullptr;
	}
	audioEngine->stop();
	blog(LOG_INFO, "Stream Mix output stopped");
}
