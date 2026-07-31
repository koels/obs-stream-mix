/*
 * Stream Mix - native streaming integration implementation.
 */
#include "stream-mix-hook.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#define blog(level, format, ...) \
	blog(level, "[stream-mix][hook] " format, ##__VA_ARGS__)

/* Fires from obs_output_start(), after OBS has assigned the stream encoder but
 * before the async connection activates it. */
static void starting_signal(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(cd);
	static_cast<StreamMixHook *>(data)->redirect_encoder();
}

void StreamMixHook::on_streaming_starting()
{
	/* Open the private mix once and keep it running. Idempotent. */
	if (!engine->start()) {
		blog(LOG_ERROR, "failed to open stream mix; the stream will "
				"use OBS's default audio");
		return;
	}

	output = obs_frontend_get_streaming_output(); /* new ref */
	if (!output) {
		blog(LOG_ERROR, "no streaming output available");
		return;
	}

	signal_handler_t *sh = obs_output_get_signal_handler(output);
	signal_handler_connect(sh, "starting", starting_signal, this);
	signal_connected = true;
	blog(LOG_INFO, "armed: waiting for streaming output to start");
}

void StreamMixHook::redirect_encoder()
{
	if (!output || !engine->handle())
		return;

	obs_encoder_t *enc = obs_output_get_audio_encoder(output, 0);
	if (!enc) {
		blog(LOG_ERROR, "streaming output has no audio encoder");
		return;
	}

	/* The decisive call: point OBS's own stream encoder at our combined
	 * mix. Allowed because the encoder is assigned but not yet active. We
	 * deliberately never restore it or free the mix on stop -- OBS
	 * disconnects the encoder asynchronously, and the mix must outlive
	 * that. */
	obs_encoder_set_audio(enc, engine->handle());
	blog(LOG_INFO, "streaming audio encoder '%s' redirected to Stream Mix",
	     obs_encoder_get_name(enc));
}

void StreamMixHook::on_streaming_stopped()
{
	/* Detach the signal and drop our output reference. Do NOT touch the
	 * encoder and do NOT stop the mix: OBS's end_data_capture_thread is
	 * (asynchronously) disconnecting the encoder from the mix right now,
	 * and tearing the mix down here is a use-after-free. The mix stays
	 * open for the plugin's lifetime. */
	if (output) {
		if (signal_connected) {
			signal_handler_t *sh =
				obs_output_get_signal_handler(output);
			signal_handler_disconnect(sh, "starting",
						  starting_signal, this);
			signal_connected = false;
		}
		obs_output_release(output);
		output = nullptr;
	}
}
