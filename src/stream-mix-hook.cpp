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
	/* Bring up the private mix so it is producing before the encoder
	 * starts pulling. */
	if (!engine->start()) {
		blog(LOG_ERROR, "failed to start stream mix; stream will use "
				"OBS's default audio");
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
		blog(LOG_ERROR, "streaming output has no audio encoder to "
				"redirect");
		return;
	}

	/* The decisive call: point OBS's own stream encoder at our combined
	 * mix. Allowed because the encoder is assigned but not yet active. */
	obs_encoder_set_audio(enc, engine->handle());
	weak_enc = obs_encoder_get_weak_encoder(enc); /* to restore later */
	blog(LOG_INFO, "streaming audio encoder '%s' redirected to Stream Mix",
	     obs_encoder_get_name(enc));
}

void StreamMixHook::on_streaming_stopped()
{
	/* Restore the encoder's original audio before we tear down our mix, so
	 * a reused encoder never references a freed audio_t. */
	if (weak_enc) {
		obs_encoder_t *enc = obs_weak_encoder_get_encoder(weak_enc);
		if (enc) {
			obs_encoder_set_audio(enc, obs_get_audio());
			obs_encoder_release(enc);
		}
		obs_weak_encoder_release(weak_enc);
		weak_enc = nullptr;
	}

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

	engine->stop();
}
