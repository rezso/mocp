#ifndef ZITA_H
#define ZITA_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "audio_conversion.h"

int zita_init (struct audio_conversion *conv);
void zita_destroy (void *zita);

float *zita_resample_sound (struct audio_conversion *conv, const float *buf,
		const size_t samples, const int nchannels, size_t *resampled_samples);

#ifdef __cplusplus
}
#endif

#endif
