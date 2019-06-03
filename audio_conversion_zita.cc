#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <zita-resampler/resampler.h>

#define DEBUG

#include "common.h"
#include "log.h"
#include "options.h"
#include "audio_conversion.h"
#include "audio_conversion_zita.h"

extern "C" {

int zita_init (struct audio_conversion *conv) {
	Resampler *zita = new Resampler;

	int err = zita->setup (conv->from.rate, conv->to.rate, conv->to.channels, options_get_int("ZitaResampleQuality"));
	
	conv->zita = (void*)zita;
	return err;
}

void zita_destroy (void *zita) {
	delete (Resampler*)zita;
}

float *zita_resample_sound (struct audio_conversion *conv, const float *buf,
		const size_t samples, const int nchannels, size_t *resampled_samples)
{
	float *output = NULL;
	Resampler *zita = (Resampler*)conv->zita;

	*resampled_samples = (int)(samples * conv->to.rate / (double)conv->from.rate / nchannels + 10) * nchannels;
	//+ 10 * nchannels);
	debug ("TG: samples: %zu, channels: %d, resampled_samples %zu", samples, nchannels, *resampled_samples);

	output = (float*)xmalloc (sizeof(float) * *resampled_samples);


	// expressed in frames
	zita->inp_count = samples/nchannels;
	zita->out_count = *resampled_samples/nchannels;
	zita->inp_data = (float*)buf; // casting away const. hope it doesn't explode.
	zita->out_data = output;

	if (zita->process()) {
		debug ("TG: zita resampler processing error!");
	}

 	debug ("TG: %zu input samples left, %zu output samples left, %zu samples output", (size_t)zita->inp_count*nchannels, (size_t)zita->out_count*nchannels, *resampled_samples - zita->out_count * nchannels);

	if (zita->inp_count != 0) {
		debug ("TG: some samples not processed by zita!");
	}

	*resampled_samples -= zita->out_count * nchannels;
	return output;
}

}
