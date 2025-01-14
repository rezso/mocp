/*
 * MOC - music on console
 * Copyright (C) 2005 Damian Pietras <daper@daper.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Code for conversion between float and fixed point types is based on
 * libsamplerate:
 * Copyright (C) 2002-2004 Erik de Castro Lopo <erikd@mega-nerd.com>
 */

/* For future: audio conversion should be performed in order:
 * channels -> rate -> format
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>

#ifdef HAVE_SAMPLERATE
# include <samplerate.h>
#endif

#define DEBUG

#include "common.h"
#include "audio_conversion.h"
#include "log.h"
#include "options.h"

static void float_to_u8 (const float *in, unsigned char *out, const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		float f = in[i] * INT32_MAX;

		if (f >= INT32_MAX)
			out[i] = UINT8_MAX;
		else if (f <= INT32_MIN)
			out[i] = 0;
		else
			out[i] = (unsigned int)((lrintf(f) >> 24) - INT8_MIN);
	}
}

static void float_to_s8 (const float *in, char *out, const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		float f = in[i] * INT32_MAX;

		if (f >= INT32_MAX)
			out[i] = INT8_MAX;
		else if (f <= INT32_MIN)
			out[i] = INT8_MIN;
		else
			out[i] = lrintf(f) >> 24;
	}
}

static void float_to_u16 (const float *in, unsigned char *out,
		const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		uint16_t *out_val = (uint16_t *)(out + i * sizeof (uint16_t));
		float f = in[i] * INT32_MAX;

		if (f >= INT32_MAX)
			*out_val = UINT16_MAX;
		else if (f <= INT32_MIN)
			*out_val = 0;
		else
			*out_val = (unsigned int)((lrintf(f) >> 16) - INT16_MIN);
	}
}

static void float_to_s16 (const float *in, char *out, const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		int16_t *out_val = (int16_t *)(out + i * sizeof (int16_t));
		float f = in[i] * INT32_MAX;

		if (f >= INT32_MAX)
			*out_val = INT16_MAX;
		else if (f <= INT32_MIN)
			*out_val = INT16_MIN;
		else
			*out_val = lrintf(f) >> 16;
	}
}

static void float_to_u24 (const float *in, unsigned char *out,
		const size_t samples)
{
	size_t i;


	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		uint32_t *out_val = (uint32_t *)(out + i * sizeof (uint32_t));
		float f = in[i] * S24_MAX;

		if (f >= S24_MAX)
			*out_val = U24_MAX;
		else if (f <= S24_MIN)
			*out_val = 0;
		else
			*out_val = (uint32_t)(lrintf(f) - S24_MIN);
	}
}

static void float_to_s24 (const float *in, char *out,
		const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		int32_t *out_val = (int32_t *)(out + i * sizeof (int32_t));
		float f = in[i] * S24_MAX;

		if (f >= S24_MAX)
			*out_val = S24_MAX;
		else if (f <= S24_MIN)
			*out_val = S24_MIN;
		else
			*out_val = lrintf(f);
	}
}

static void float_to_u24_3 (const float *in, unsigned char *out, const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		int8_t *out_val = (int8_t *)(out + 3*i);
		float f = in[i] * S24_MAX;
		uint32_t out_i;

		if (f >= S24_MAX)
			out_i = U24_MAX;
		else if (f <= S24_MIN)
			out_i = 0;
		else
			out_i = (uint32_t)(lrintf(f) - S24_MIN);
		out_val[0] = (out_i&0x000000FF);
		out_val[1] = (out_i&0x0000FF00)>>8;
		out_val[2] = (out_i&0x00FF0000)>>16;
	}
}

static void float_to_s24_3 (const float *in, char *out, const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	int32_t out_i;
	for (i = 0; i < samples; i++) {
		int8_t *out_val = (int8_t *)(out + 3*i);
		float f = in[i] * S24_MAX;

		if (f >= S24_MAX)
			out_i = S24_MAX;
		else if (f <= S24_MIN)
			out_i = S24_MIN;
		else
			out_i = lrintf(f);
		out_val[0] = (out_i&0x000000FF);
		out_val[1] = (out_i&0x0000FF00)>>8;
		out_val[2] = (out_i&0x00FF0000)>>16;
	}
}


static void float_to_u32 (const float *in, unsigned char *out,
		const size_t samples)
{
	size_t i;


	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		uint32_t *out_val = (uint32_t *)(out + i * sizeof (uint32_t));
		float f = in[i] * INT32_MAX;

		if (f >= INT32_MAX)
			*out_val = INT32_MAX;
		else if (f <= INT32_MIN)
			*out_val = 0;
		else
			*out_val = (uint32_t)(lrintf(f) - INT32_MIN);
	}
}

static void float_to_s32 (const float *in, char *out, const size_t samples)
{
	size_t i;


	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
		int32_t *out_val = (int32_t *)(out + i * sizeof (int32_t));
		float f = in[i] * INT32_MAX;

		if (f >= INT32_MAX)
			*out_val = INT32_MAX;
		else if (f <= INT32_MIN)
			*out_val = INT32_MIN;
		else
			*out_val = lrintf(f);
	}
}

static void u8_to_float (const unsigned char *in, float *out,
		const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = (((int)*in++) + INT8_MIN) / (float)(INT8_MAX + 1);
}

static void s8_to_float (const char *in, float *out, const size_t samples)
{
	size_t i;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = *in++ / (float)(INT8_MAX + 1);
}

static void u16_to_float (const unsigned char *in, float *out,
		const size_t samples)
{
	size_t i;
	const uint16_t *in_16 = (uint16_t *)in;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = ((int)*in_16++ + INT16_MIN) / (float)(INT16_MAX + 1);
}

static void s16_to_float (const char *in, float *out, const size_t samples)
{
	size_t i;
	const int16_t *in_16 = (int16_t *)in;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = *in_16++ / (float)(INT16_MAX + 1);
}

static void u24_to_float (const unsigned char *in, float *out,
		const size_t samples)
{  
	size_t i;
	const uint32_t *in_32 = (uint32_t *)in;
	
	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = ((float)*in_32++ + (float)S24_MIN) / ((float)S24_MAX + 1.0);
}

static void s24_to_float (const char *in, float *out,
		const size_t samples)
{
	size_t i;
	const int32_t *in_32 = (int32_t *)in;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = *in_32++ / ((float)S24_MAX + 1.0);
}

static void s24_3_to_float (const char *in, float *out,
		const size_t samples)
{
	size_t i;
	const int8_t *in_8 = (int8_t *)in;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
#ifdef WORDS_BIGENDIAN
		out[i] = (*(in_8+2)+(*(in_8+1)<<8)+(*(in_8)<<16)) / ((float)S24_MAX + 1.0);
#else
		out[i] = (*(in_8)+(*(in_8+1)<<8)+(*(in_8+2)<<16)) / ((float)S24_MAX + 1.0);
#endif
		in_8+=3;
	}
}

static void u24_3_to_float (const char *in, float *out,
		const size_t samples)
{
	size_t i;
	const uint8_t *in_8 = (uint8_t *)in;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++) {
#ifdef WORDS_BIGENDIAN
		out[i] = (*(in_8+2)+(*(in_8+1)<<8)+(*(in_8)<<16) + (float)S24_MIN) / ((float)S24_MAX + 1.0);
#else
		out[i] = (*(in_8)+(*(in_8+1)<<8)+(*(in_8+2)<<16) + (float)S24_MIN) / ((float)S24_MAX + 1.0);
#endif
		in_8+=3;
	}
}

static void u32_to_float (const unsigned char *in, float *out,
		const size_t samples)
{
	size_t i;
	const uint32_t *in_32 = (uint32_t *)in;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = ((float)*in_32++ + (float)INT32_MIN) / ((float)INT32_MAX + 1.0);
}

static void s32_to_float (const char *in, float *out, const size_t samples)
{
	size_t i;
	const int32_t *in_32 = (int32_t *)in;

	assert (in != NULL);
	assert (out != NULL);

	for (i = 0; i < samples; i++)
		out[i] = *in_32++ / ((float)INT32_MAX + 1.0);
}

/* Convert fixed point samples in format fmt (size in bytes) to float.
 * Size of converted sound is put in new_size. Returned memory is malloc()ed. */
static float *fixed_to_float (const char *buf, const size_t size,
		const long fmt, size_t *new_size)
{
	float *out = NULL;
	char fmt_name[SFMT_STR_MAX];

	assert ((fmt & SFMT_MASK_FORMAT) != SFMT_FLOAT);

	switch (fmt & SFMT_MASK_FORMAT) {
		case SFMT_U8:
			*new_size = sizeof(float) * size;
			out = (float *)xmalloc (*new_size);
			u8_to_float ((unsigned char *)buf, out, size);
			break;
		case SFMT_S8:
			*new_size = sizeof(float) * size;
			out = (float *)xmalloc (*new_size);
			s8_to_float (buf, out, size);
			break;
		case SFMT_U16:
			*new_size = sizeof(float) * size / 2;
			out = (float *)xmalloc (*new_size);
			u16_to_float ((unsigned char *)buf, out, size / 2);
			break;
		case SFMT_S16:
			*new_size = sizeof(float) * size / 2;
			out = (float *)xmalloc (*new_size);
			s16_to_float (buf, out, size / 2);
			break;
		case SFMT_U24:
			*new_size = sizeof(float) * size / 4;
			out = (float *)xmalloc (*new_size);
			u24_to_float ((unsigned char *)buf, out, size / 4);
			break;
		case SFMT_S24:
			*new_size = sizeof(float) * size / 4;
			out = (float *)xmalloc (*new_size);
			s24_to_float (buf, out, size / 4);
			break;
		case SFMT_S24_3:
			*new_size = sizeof(float) * size / 3;
			out = (float *)xmalloc (*new_size);
			s24_3_to_float (buf, out, size / 3);
			break;
		case SFMT_U24_3:
			*new_size = sizeof(float) * size / 3;
			out = (float *)xmalloc (*new_size);
			u24_3_to_float (buf, out, size / 3);
			break;
		case SFMT_U32:
			*new_size = sizeof(float) * size / 4;
			out = (float *)xmalloc (*new_size);
			u32_to_float ((unsigned char *)buf, out, size / 4);
			break;
		case SFMT_S32:
			*new_size = sizeof(float) * size / 4;
			out = (float *)xmalloc (*new_size);
			s32_to_float (buf, out, size / 4);
			break;
		default:
			error ("Can't convert from %s to float!",
			       sfmt_str (fmt, fmt_name, sizeof (fmt_name)));
			abort ();
	}

	return out;
}

/* Convert float samples to fixed point format fmt. Returned samples of size
 * new_size bytes is malloc()ed. */
static char *float_to_fixed (const float *buf, const size_t samples,
		const long fmt, size_t *new_size)
{
	char fmt_name[SFMT_STR_MAX];
	char *new_snd = NULL;

	assert ((fmt & SFMT_MASK_FORMAT) != SFMT_FLOAT);

	switch (fmt & SFMT_MASK_FORMAT) {
		case SFMT_U8:
			*new_size = samples;
			new_snd = (char *)xmalloc (samples);
			float_to_u8 (buf, (unsigned char *)new_snd, samples);
			break;
		case SFMT_S8:
			*new_size = samples;
			new_snd = (char *)xmalloc (samples);
			float_to_s8 (buf, new_snd, samples);
			break;
		case SFMT_U16:
			*new_size = samples * 2;
			new_snd = (char *)xmalloc (*new_size);
			float_to_u16 (buf, (unsigned char *)new_snd, samples);
			break;
		case SFMT_S16:
			*new_size = samples * 2;
			new_snd = (char *)xmalloc (*new_size);
			float_to_s16 (buf, new_snd, samples);
			break;
		case SFMT_U24:
			*new_size = samples * 4;
			new_snd = (char *)xmalloc (*new_size);
			float_to_u24 (buf, (unsigned char *)new_snd, samples);
			break;
		case SFMT_S24:
			*new_size = samples * 4;
			new_snd = (char *)xmalloc (*new_size);
			float_to_s24 (buf, new_snd, samples);
			break;
		case SFMT_U24_3:
			*new_size = samples * 3;
			new_snd = (char *)xmalloc (*new_size);
			float_to_u24_3 (buf, (unsigned char *)new_snd, samples);
			break;
		case SFMT_S24_3:
			*new_size = samples * 3;
			new_snd = (char *)xmalloc (*new_size);
			float_to_s24_3 (buf, new_snd, samples);
			break;
		case SFMT_U32:
			*new_size = samples * 4;
			new_snd = (char *)xmalloc (*new_size);
			float_to_u32 (buf, (unsigned char *)new_snd, samples);
			break;
		case SFMT_S32:
			*new_size = samples * 4;
			new_snd = (char *)xmalloc (*new_size);
			float_to_s32 (buf, new_snd, samples);
			break;
		default:
			error ("Can't convert from float to %s!",
			       sfmt_str (fmt, fmt_name, sizeof (fmt_name)));
			abort ();
	}

	return new_snd;
}

static inline void change_sign_8 (uint8_t *buf, const size_t samples)
{
	size_t i;

	for (i = 0; i < samples; i++)
		*buf++ ^= 1 << 7;
}

static inline void change_sign_16 (uint16_t *buf, const size_t samples)
{
	size_t i;

	for (i = 0; i < samples; i++)
		*buf++ ^= 1 << 15;
}

static inline void change_sign_24 (uint32_t *buf, const size_t samples)
{
	size_t i;

	for (i = 0; i < samples; i++)
		*buf++ ^= 1 << 23;
}

static inline void change_sign_32 (uint32_t *buf, const size_t samples)
{
	size_t i;

	for (i = 0; i < samples; i++)
		*buf++ ^= 1 << 31;
}

/* Change the signs of samples in format *fmt.  Also changes fmt to the new
 * format. */
static void change_sign (char *buf, const size_t size, long *fmt)
{
	char fmt_name[SFMT_STR_MAX];

	switch (*fmt & SFMT_MASK_FORMAT) {
		case SFMT_S8:
		case SFMT_U8:
			change_sign_8 ((uint8_t *)buf, size);
			if (*fmt & SFMT_S8)
				*fmt = sfmt_set_fmt (*fmt, SFMT_U8);
			else
				*fmt = sfmt_set_fmt (*fmt, SFMT_S8);
			break;
		case SFMT_S16:
		case SFMT_U16:
			change_sign_16 ((uint16_t *)buf, size / 2);
			if (*fmt & SFMT_S16)
				*fmt = sfmt_set_fmt (*fmt, SFMT_U16);
			else
				*fmt = sfmt_set_fmt (*fmt, SFMT_S16);
			break;
		case SFMT_S24:
		case SFMT_U24:
			change_sign_24 ((uint32_t *)buf, size/4);
			if (*fmt & SFMT_S24)
				*fmt = sfmt_set_fmt (*fmt, SFMT_U24);
			else
				*fmt = sfmt_set_fmt (*fmt, SFMT_S24);
			break;
		case SFMT_S32:
		case SFMT_U32:
			change_sign_32 ((uint32_t *)buf, size/4);
			if (*fmt & SFMT_S32)
				*fmt = sfmt_set_fmt (*fmt, SFMT_U32);
			else
				*fmt = sfmt_set_fmt (*fmt, SFMT_S32);
			break;
		default:
			error ("Request for changing sign of unknown format: %s",
			       sfmt_str (*fmt, fmt_name, sizeof (fmt_name)));
			abort ();
	}
}

static inline void audio_conv_bswap_16 (int16_t *buf, const size_t num)
{
	size_t i;

	for (i = 0; i < num; i++)
		buf[i] = bswap_16 (buf[i]);
}

static inline void audio_conv_bswap_24 (int8_t *buf, const size_t num)
{
	size_t i;
        int8_t tmp;

	for (i = 0; i < num; i+=3) {
		tmp = buf[i];
                buf[i] = buf[i+2];
                buf[i+2] = tmp;
        }
}

static inline void audio_conv_bswap_32 (int32_t *buf, const size_t num)
{
	size_t i;

	for (i = 0; i < num; i++)
		buf[i] = bswap_32 (buf[i]);
}

/* Swap endianness of fixed point samples. */
static void swap_endian (char *buf, const size_t size, const long fmt)
{
	if ((fmt & (SFMT_S8 | SFMT_U8 | SFMT_FLOAT)))
		return;
        debug("Swapping endianness");

	switch (fmt & SFMT_MASK_FORMAT) {
		case SFMT_S16:
		case SFMT_U16:
			audio_conv_bswap_16 ((int16_t *)buf, size / 2);
			break;
		case SFMT_S24:
		case SFMT_U24:
		case SFMT_S32:
		case SFMT_U32:
			audio_conv_bswap_32 ((int32_t *)buf, size / 4);
			break;
		case SFMT_S24_3:
		case SFMT_U24_3:
			audio_conv_bswap_24 ((int8_t *)buf, size);
                        break;
                default:
			error ("Can't convert to native endian!");
			abort (); /* we can't do anything smarter */
	}
}

/* Initialize the audio_conversion structure for conversion between parameters
 * from and to. Return 0 on error. */
int audio_conv_new (struct audio_conversion *conv,
		const struct sound_params *from,
		const struct sound_params *to)
{
	assert (from->rate != to->rate || from->fmt != to->fmt
			|| from->channels != to->channels);

	if (from->channels != to->channels) {

		/* the only conversion we can do */
		if (!((from->channels == 1 || from->channels == 6) && to->channels == 2)) {
			error ("Can't change number of channels (%d to %d)!",
			        from->channels, to->channels);
			return 0;
		}
	}

	if (from->rate != to->rate) {
		if (options_get_int("EnableResample")==0) {
			error ("Resampling disabled!");
			return 0;
		}
#ifdef HAVE_SAMPLERATE
		int err;
		int resample_type = -1;
		char *method = options_get_symb ("ResampleMethod");

		if (!strcasecmp(method, "SincBestQuality"))
			resample_type = SRC_SINC_BEST_QUALITY;
		else if (!strcasecmp(method, "SincMediumQuality"))
			resample_type = SRC_SINC_MEDIUM_QUALITY;
		else if (!strcasecmp(method, "SincFastest"))
			resample_type = SRC_SINC_FASTEST;
		else if (!strcasecmp(method, "ZeroOrderHold"))
			resample_type = SRC_ZERO_ORDER_HOLD;
		else if (!strcasecmp(method, "Linear"))
			resample_type = SRC_LINEAR;
		else
			fatal ("Bad ResampleMethod option: %s", method);

		conv->src_state = src_new (resample_type, from->channels, &err);
		if (!conv->src_state) {
			error ("Can't resample from %dHz to %dHz: %s",
					from->rate, to->rate, src_strerror (err));
			return 0;
		}
		logit ("Resampling from %dHz to %dHz using %s",from->rate, to->rate, method);
#else
		error ("Resampling not supported!");
		return 0;
#endif
	}
#ifdef HAVE_SAMPLERATE
	else
		conv->src_state = NULL;
#endif

	conv->from = *from;
	conv->to = *to;

#ifdef HAVE_SAMPLERATE
	conv->resample_buf = NULL;
	conv->resample_buf_nsamples = 0;
#endif

	return 1;
}

#ifdef HAVE_SAMPLERATE
static float *resample_sound (struct audio_conversion *conv, const float *buf,
		const size_t samples, const int nchannels, size_t *resampled_samples)
{
	SRC_DATA resample_data;
	float *output;
	float *new_input_start;
	int output_samples = 0;

	resample_data.end_of_input = 0;
	resample_data.src_ratio = conv->to.rate / (double)conv->from.rate;

	resample_data.input_frames = samples / nchannels
		+ conv->resample_buf_nsamples / nchannels;
	resample_data.output_frames = resample_data.input_frames
		* resample_data.src_ratio;

	assert (conv->resample_buf || conv->resample_buf_nsamples == 0);
	conv->resample_buf = xrealloc (conv->resample_buf,
	                               sizeof(float) * nchannels *
	                               resample_data.input_frames);
	new_input_start = conv->resample_buf + conv->resample_buf_nsamples;

	output = (float *)xmalloc (sizeof(float) * resample_data.output_frames
				* nchannels);

	/*debug ("Resampling %lu bytes of data by ratio %f", (unsigned long)size,
			resample_data.src_ratio);*/

	memcpy (new_input_start, buf, samples * sizeof(float));
	resample_data.data_in = conv->resample_buf;
	resample_data.data_out = output;

	do {
		int err;

		if ((err = src_process(conv->src_state, &resample_data))) {
			error ("Can't resample: %s", src_strerror (err));
			free (output);
			return NULL;
		}

		resample_data.data_in += resample_data.input_frames_used
			* nchannels;
		resample_data.input_frames -= resample_data.input_frames_used;
		resample_data.data_out += resample_data.output_frames_gen
			* nchannels;
		resample_data.output_frames -= resample_data.output_frames_gen;
		output_samples += resample_data.output_frames_gen * nchannels;
	} while (resample_data.input_frames && resample_data.output_frames_gen
			&& resample_data.output_frames);

	*resampled_samples = output_samples;

	if (resample_data.input_frames) {
		conv->resample_buf_nsamples = resample_data.input_frames
			* nchannels;
		if (conv->resample_buf != resample_data.data_in) {
			float *new;

			new = (float *)xmalloc (sizeof(float) *
					conv->resample_buf_nsamples);
			memcpy (new, resample_data.data_in, sizeof(float) *
					conv->resample_buf_nsamples);
			free (conv->resample_buf);
			conv->resample_buf = new;
		}
	}
	else {
		free (conv->resample_buf);
		conv->resample_buf = NULL;
		conv->resample_buf_nsamples = 0;
	}

	return output;
}
#endif

/* Double the channels from */
static char *mono_to_stereo (const char *mono, const size_t size,
		const long format)
{
	int Bps = sfmt_Bps (format);
	size_t i;
	char *stereo;

	stereo = (char *)xmalloc (size * 2);

	for (i = 0; i < size; i += Bps) {
		memcpy (stereo + (i * 2), mono + i, Bps);
		memcpy (stereo + (i * 2 + Bps), mono + i, Bps);
	}

	return stereo;
}

/* DPL downmix: 5.1 -> stereo */
static char *ch6_to_stereo (const char *ch6, const size_t size,
		const long format)
{
	debug("Downmixing from 5.1 to 2.0");
	int Bps = sfmt_Bps (format);
	size_t i;
	int j,k;
	char *stereo;

	stereo = (char *)xmalloc (size / 3);
	float a[2][6]; //downmix matrix
a[0][0] = 1.0; a[0][2]=0.707; a[0][1]=0; a[0][4]=-0.8165; a[0][5]= -0.5774; a[0][3]=0.707;
a[1][0] = 0; a[1][2]=0.707; a[1][1]=1.0; a[1][4]= 0.5774; a[1][5]=0.8165; a[1][3]=0.707;
	const float normalization = 0.2626;

	if (format & SFMT_S16)
	{  
	debug("Downmixing from 5.1 to 2.0: S16");

	int16_t sample_in[6];
	int16_t sample_out[2];

	for (i = 0; i < size; i += 6*Bps) {
		memcpy(&sample_in,ch6 + i,Bps*6);
		sample_out[0]=0; sample_out[1]=0;
		for (j=0; j<2; j++)
		    for (k=0; k<6; k++)
		      sample_out[j]+=a[j][k]*sample_in[k]*normalization;
		memcpy (stereo + (i /3), sample_out, 2*Bps);
	}
	}
	else if (format & SFMT_FLOAT)
	{  
	debug("Downmixing from 5.1 to 2.0: FLOAT");

	float sample_in[6];
	float sample_out[2];

	for (i = 0; i < size; i += 6*Bps) {
		memcpy(&sample_in,ch6 + i,Bps*6);
		sample_out[0]=0; sample_out[1]=0;
		for (j=0; j<2; j++)
		    for (k=0; k<6; k++)
			  sample_out[j]+=(a[j][k]*sample_in[k]*normalization);
		memcpy (stereo + (i / 3), sample_out, 2*Bps);
	}
	}
	else if (format & SFMT_S32)
	{  
	debug("Downmixing from 5.1 to 2.0: S32");

	int32_t sample_in[6];
	int32_t sample_out[2];

	for (i = 0; i < size; i += 6*Bps) {
		memcpy(&sample_in,ch6 + i,Bps*6);
		sample_out[0]=0; sample_out[1]=0;
		for (j=0; j<2; j++)
		    for (k=0; k<6; k++)
			  sample_out[j]+=(a[j][k]*sample_in[k]*normalization);
		memcpy (stereo + (i / 3), sample_out, 2*Bps);
	}
	}
	else
	{
	error("Can't downsample that sample format yet.");
	abort ();
	}
	return stereo;
}

static int8_t *s32_to_s24_3 (int32_t *in, const size_t samples)
{
	size_t i;
	int8_t *new;

	new = (int8_t *)xmalloc (samples * 3);

	for (i = 0; i < samples; i++)
	{
		new[3*i] = (in[i]&0x0000FF00)>>8;
		new[3*i+1] = (in[i]&0x00FF0000)>>16;
		new[3*i+2] = (in[i]&0xFF000000)>>24;
	}
	return new;
}


static int16_t *s32_to_s16 (int32_t *in, const size_t samples)
{
	size_t i;
	int16_t *new;

	new = (int16_t *)xmalloc (samples * 2);

	for (i = 0; i < samples; i++)
		new[i] = in[i] >> 16;

	return new;
}

static uint16_t *u32_to_u16 (uint32_t *in, const size_t samples)
{
	size_t i;
	uint16_t *new;

	new = (uint16_t *)xmalloc (samples * 2);

	for (i = 0; i < samples; i++)
		new[i] = in[i] >> 16;

	return new;
}

static int32_t *s32_to_s24 (int32_t *in, const size_t samples)
{
	size_t i;
	int32_t *new;

	new = (int32_t *)xmalloc (samples * 4);

	for (i = 0; i < samples; i++)
		new[i] = in[i] >> 8;

	return new;
}

static uint32_t *u32_to_u24 (uint32_t *in, const size_t samples)
{
	size_t i;
	uint32_t *new;

	new = (uint32_t *)xmalloc (samples * 4);

	for (i = 0; i < samples; i++)
		new[i] = in[i] >> 8;

	return new;
}

static int16_t *s24_to_s16 (int32_t *in, const size_t samples)
{
	size_t i;
	int16_t *new;

	new = (int16_t *)xmalloc (samples * 2);

	for (i = 0; i < samples; i++)
		new[i] = in[i] >> 8;

	return new;
}

static uint16_t *u24_to_u16 (uint32_t *in, const size_t samples)
{
	size_t i;
	uint16_t *new;

	new = (uint16_t *)xmalloc (samples * 2);

	for (i = 0; i < samples; i++)
		new[i] = in[i] >> 8;

	return new;
}

/* Do the sound conversion.  buf of length size is the sample buffer to
 * convert and the size of the converted sound is put into *conv_len.
 * Return the converted sound in malloc()ed memory.
 *
 * Conversion workflow:
 *   1. Change endianness
 *   2. Change sample format (to float or target SFMT if no resampling needed)
 *   3. Resample
 *   4. Change sample format to destination SFMT
 *   5. Up/downmix channels
*/
char *audio_conv (struct audio_conversion *conv, const char *buf,
		const size_t size, size_t *conv_len)
{
	char *curr_sound;
	long curr_sfmt = conv->from.fmt;

	*conv_len = size;

	curr_sound = (char *)xmalloc (size);
	memcpy (curr_sound, buf, size);

	if (!(curr_sfmt & SFMT_NE)) {
		swap_endian (curr_sound, *conv_len, curr_sfmt);
		curr_sfmt = sfmt_set_endian (curr_sfmt, SFMT_NE);
	}

	/* Special case (optimization): 32bit -> 24bit_3 */
	if ((curr_sfmt & (SFMT_S32 | SFMT_U32)) &&
	    (conv->to.fmt & (SFMT_S24_3 | SFMT_U24_3)) &&
	    conv->from.rate == conv->to.rate) {
		char *new_sound;

		if ((curr_sfmt & SFMT_MASK_FORMAT) == SFMT_S32) {
			new_sound = (char *)s32_to_s24_3 ((int32_t *)curr_sound,
					*conv_len / 4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_S24_3);
		}
		else {
			new_sound = (char *)s32_to_s24_3 ((int32_t *)curr_sound,
					*conv_len / 4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_U24_3);
		}

		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
		*conv_len = *conv_len *3/ 4;

		logit ("Fast conversion: 32bit -> 24_3bit!");
	}
	
	/* Special case (optimization): if we only need to convert 32bit samples
	 * to 16bit, we can do it very simply and quickly. */
	if ((curr_sfmt & (SFMT_S32 | SFMT_U32)) &&
	    (conv->to.fmt & (SFMT_S16 | SFMT_U16)) &&
	    conv->from.rate == conv->to.rate) {
		char *new_sound;

		if ((curr_sfmt & SFMT_MASK_FORMAT) == SFMT_S32) {
			new_sound = (char *)s32_to_s16 ((int32_t *)curr_sound,
					*conv_len / 4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_S16);
		}
		else {
			new_sound = (char *)u32_to_u16 ((uint32_t *)curr_sound,
					*conv_len / 4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_U16);
		}

		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
		*conv_len /= 2;

		logit ("Fast conversion: 32bit -> 16bit!");
	}

	/* Special case (optimization): 32bit to 24bit */
 	if ((curr_sfmt & (SFMT_S32 | SFMT_U32)) &&
 	    (conv->to.fmt & (SFMT_S24 | SFMT_U24)) &&
 	    conv->from.rate == conv->to.rate) {
		char *new_sound;

		if ((curr_sfmt & SFMT_MASK_FORMAT) == SFMT_S32) {
			new_sound = (char *)s32_to_s24 ((int32_t *)curr_sound,
					*conv_len/4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_S24);
		}
		else {
			new_sound = (char *)u32_to_u24 ((uint32_t *)curr_sound,
					*conv_len/4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_U24);
		}

		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
		//*conv_len /= 2;

		logit ("Fast conversion: 32bit -> 24bit!");
	}

	/* Special case (optimization): 24bit to 16bit */
	if ((curr_sfmt & (SFMT_S24 | SFMT_U24)) &&
	    (conv->to.fmt & (SFMT_S16 | SFMT_U16)) &&
	    conv->from.rate == conv->to.rate) {
		char *new_sound;

		if ((curr_sfmt & SFMT_MASK_FORMAT) == SFMT_S24) {
			new_sound = (char *)s24_to_s16 ((int32_t *)curr_sound,
					*conv_len / 4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_S16);
		}
		else {
			new_sound = (char *)u24_to_u16 ((uint32_t *)curr_sound,
					*conv_len / 4);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_U16);
		}

		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
		*conv_len /= 2;

		logit ("Fast conversion: 24bit -> 16bit!");
	}

	
	/* convert to float if necessary */
	if ((conv->from.rate != conv->to.rate
				|| (conv->to.fmt & SFMT_MASK_FORMAT) == SFMT_FLOAT
				|| !sfmt_same_bps(conv->to.fmt, curr_sfmt))
			&& (curr_sfmt & SFMT_MASK_FORMAT) != SFMT_FLOAT) {
		char *new_sound;

		new_sound = (char *)fixed_to_float (curr_sound, *conv_len,
				curr_sfmt, conv_len);
		curr_sfmt = sfmt_set_fmt (curr_sfmt, SFMT_FLOAT);

		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
	}

#ifdef HAVE_SAMPLERATE
	if (conv->from.rate != conv->to.rate) {
		char *new_sound = (char *)resample_sound (conv,
				(float *)curr_sound,
				*conv_len / sizeof(float), conv->from.channels,
				conv_len);
		*conv_len *= sizeof(float);
		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
	}
#endif

	if ((curr_sfmt & SFMT_MASK_FORMAT)
			!= (conv->to.fmt & SFMT_MASK_FORMAT)) {

		if (sfmt_same_bps(curr_sfmt, conv->to.fmt))
			change_sign (curr_sound, size, &curr_sfmt);
		else {
			char *new_sound;

			assert (curr_sfmt & SFMT_FLOAT);

			new_sound = float_to_fixed ((float *)curr_sound,
					*conv_len / sizeof(float),
					conv->to.fmt, conv_len);
			curr_sfmt = sfmt_set_fmt (curr_sfmt, conv->to.fmt);

			if (curr_sound != buf)
				free (curr_sound);
			curr_sound = new_sound;
		}
	}

	if (conv->from.channels == 1 && conv->to.channels == 2) {
		char *new_sound;

		new_sound = mono_to_stereo (curr_sound, *conv_len, curr_sfmt);
		*conv_len *= 2;

		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
	}

	if (conv->from.channels == 6 && conv->to.channels == 2) {
		char *new_sound;

		new_sound = ch6_to_stereo (curr_sound, *conv_len,
				conv->from.fmt);
		*conv_len /= 3;

		if (curr_sound != buf)
			free (curr_sound);
		curr_sound = new_sound;
	}

	if ((curr_sfmt & SFMT_MASK_ENDIANNESS)
			!= (conv->to.fmt & SFMT_MASK_ENDIANNESS)) {
		swap_endian (curr_sound, *conv_len, curr_sfmt);
		curr_sfmt = sfmt_set_endian (curr_sfmt,
				conv->to.fmt & SFMT_MASK_ENDIANNESS);
	}
	
	return curr_sound;
}

void audio_conv_destroy (struct audio_conversion *conv ASSERT_ONLY)
{
	assert (conv != NULL);

#ifdef HAVE_SAMPLERATE
	if (conv->resample_buf)
		free (conv->resample_buf);
	if (conv->src_state)
		src_delete (conv->src_state);
#endif
}
