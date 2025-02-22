/*
 * MOC - music on console
 * Copyright (C) 2011 Marien Zwart <marienz@marienz.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* PulseAudio backend.
 *
 * FEATURES:
 *
 * Does not autostart a PulseAudio server, but uses an already-started
 * one, which should be better than alsa-through-pulse.
 *
 * Supports control of either our stream's or our entire sink's volume
 * while we are actually playing. Volume control while paused is
 * intentionally unsupported: the PulseAudio documentation strongly
 * suggests not passing in an initial volume when creating a stream
 * (allowing the server to track this instead), and we do not know
 * which sink to control if we do not have a stream open.
 *
 * IMPLEMENTATION:
 *
 * Most client-side (resource allocation) errors are fatal. Failure to
 * create a server context or stream is not fatal (and MOC should cope
 * with these failures too), but server communication failures later
 * on are currently not handled (MOC has no great way for us to tell
 * it we no longer work, and I am not sure if attempting to reconnect
 * is worth it or even a good idea).
 *
 * The pulse "simple" API is too simple: it combines connecting to the
 * server and opening a stream into one operation, while I want to
 * connect to the server when MOC starts (and fall back to a different
 * backend if there is no server), and I cannot open a stream at that
 * time since I do not know the audio format yet.
 *
 * PulseAudio strongly recommends we use a high-latency connection,
 * which the MOC frontend code might not expect from its audio
 * backend. We'll see.
 *
 * We map MOC's percentage volumes linearly to pulse's PA_VOLUME_MUTED
 * (0) .. PA_VOLUME_NORM range. This is what the PulseAudio docs recommend
 * ( http://pulseaudio.org/wiki/WritingVolumeControlUIs ). It does mean
 * PulseAudio volumes above PA_VOLUME_NORM do not work well with MOC.
 *
 * Comments in audio.h claim "All functions are executed only by one
 * thread" (referring to the function in the hw_funcs struct). This is
 * a blatant lie. Most of them are invoked off the "output buffer"
 * thread (out_buf.c) but at least the "playing" thread (audio.c)
 * calls audio_close which calls our close function. We can mostly
 * ignore this problem because we serialize on the pulseaudio threaded
 * mainloop lock. But it does mean that functions that are normally
 * only called between open and close (like reset) are sometimes
 * called without us having a stream. Bulletproof, therefore:
 * serialize setting/unsetting our global stream using the threaded
 * mainloop lock, and check for that stream being non-null before
 * using it.
 *
 * I am not convinced there are no further dragons lurking here: can
 * the "playing" thread(s) close and reopen our output stream while
 * the "output buffer" thread is sending output there? We can bail if
 * our stream is simply closed, but we do not currently detect it
 * being reopened and no longer using the same sample format, which
 * might have interesting results...
 *
 * Also, read_mixer is called from the main server thread (handling
 * commands). This crashed me once when it got at a stream that was in
 * the "creating" state and therefore did not have a valid stream
 * index yet. Fixed by only assigning to the stream global when the
 * stream is valid.
 */

#include <pulse/proplist.h>
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define DEBUG

#include <math.h>
#include <pulse/pulseaudio.h>
#include "common.h"
#include "log.h"
#include "audio.h"


/* The pulse mainloop and context are initialized in pulse_init and
 * destroyed in pulse_shutdown.
 */
static pa_threaded_mainloop *mainloop = NULL;
static pa_context *context = NULL;
static uint32_t pa_default_sink_index = 0;

/* The stream is initialized in pulse_open and destroyed in pulse_close. */
static pa_stream *stream = NULL;

static int showing_sink_volume = 1;

/* Callbacks that do nothing but wake up the mainloop. */

static void context_state_callback (pa_context *context ATTR_UNUSED,
				    void *userdata)
{
	pa_threaded_mainloop *m = userdata;

	pa_threaded_mainloop_signal (m, 0);
}

static void stream_state_callback (pa_stream *stream ATTR_UNUSED,
				   void *userdata)
{
	pa_threaded_mainloop *m = userdata;

	pa_threaded_mainloop_signal (m, 0);
}

static void stream_write_callback (pa_stream *stream ATTR_UNUSED,
				   size_t nbytes ATTR_UNUSED, void *userdata)
{
	pa_threaded_mainloop *m = userdata;

	pa_threaded_mainloop_signal (m, 0);
}

/* Initialize pulse mainloop and context. Failure to connect to the
 * pulse daemon is nonfatal, everything else is fatal (as it
 * presumably means we ran out of resources).
 */
static int pulse_init (struct output_driver_caps *caps)
{
	pa_context *c;
	pa_proplist *proplist;

	assert (!mainloop);
	assert (!context);

	mainloop = pa_threaded_mainloop_new ();
	if (!mainloop)
		fatal ("Cannot create PulseAudio mainloop");

	if (pa_threaded_mainloop_start (mainloop) < 0)
		fatal ("Cannot start PulseAudio mainloop");

	/* TODO: possibly add more props.
	 *
	 * There are a few we could set in proplist.h but nothing I
	 * expect to be very useful.
	 *
	 * http://pulseaudio.org/wiki/ApplicationProperties recommends
	 * setting at least application.name, icon.name and media.role.
	 *
	 * No need to set application.name here, the name passed to
	 * pa_context_new_with_proplist overrides it.
	 */
	proplist = pa_proplist_new ();
	if (!proplist)
		fatal ("Cannot allocate PulseAudio proplist");

	pa_proplist_sets (proplist,
			  PA_PROP_APPLICATION_VERSION, PACKAGE_VERSION);
	pa_proplist_sets (proplist, PA_PROP_MEDIA_ROLE, "music");
	pa_proplist_sets (proplist, PA_PROP_APPLICATION_ID, "net.daper.moc");

	pa_threaded_mainloop_lock (mainloop);

	c = pa_context_new_with_proplist (
		pa_threaded_mainloop_get_api (mainloop),
		PACKAGE_NAME, proplist);
	pa_proplist_free (proplist);

	if (!c)
		fatal ("Cannot allocate PulseAudio context");

	pa_context_set_state_callback (c, context_state_callback, mainloop);

	/* Ignore return value, rely on state being set properly */
	pa_context_connect (c, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);

	while (1) {
		pa_context_state_t state = pa_context_get_state (c);

		if (state == PA_CONTEXT_READY)
			break;

		if (!PA_CONTEXT_IS_GOOD (state)) {
			error ("PulseAudio connection failed: %s",
			       pa_strerror (pa_context_errno (c)));

			goto unlock_and_fail;
		}

		debug ("waiting for context to become ready...");
		pa_threaded_mainloop_wait (mainloop);
	}

	/* Only set the global now that the context is actually ready */
	context = c;

	pa_threaded_mainloop_unlock (mainloop);

	/* We just make up the hardware capabilities, since pulse is
	 * supposed to be abstracting these out. Assume pulse will
	 * deal with anything we want to throw at it, and that we will
	 * only want mono or stereo audio.
	 */
	caps->min_channels = 1;
	caps->max_channels = 6;
	caps->min_rate = 0;
	caps->max_rate = 192000;
	caps->formats = (SFMT_S8 | SFMT_S16 | SFMT_S32 |
			 SFMT_FLOAT | SFMT_NE);

	return 1;

unlock_and_fail:

	pa_context_unref (c);

	pa_threaded_mainloop_unlock (mainloop);

	pa_threaded_mainloop_stop (mainloop);
	pa_threaded_mainloop_free (mainloop);
	mainloop = NULL;

	return 0;
}

static void pulse_shutdown (void)
{
	pa_threaded_mainloop_lock (mainloop);

	pa_context_disconnect (context);
	pa_context_unref (context);
	context = NULL;

	pa_threaded_mainloop_unlock (mainloop);

	pa_threaded_mainloop_stop (mainloop);
	pa_threaded_mainloop_free (mainloop);
	mainloop = NULL;
}

static int pulse_open (struct sound_params *sound_params)
{
	pa_sample_spec ss;
	pa_buffer_attr ba;
	pa_stream *s;

	assert (!stream);
	/* Initialize everything to -1, which in practice gets us
	 * about 2 seconds of latency (which is fine). This is not the
	 * same as passing NULL for this struct, which gets us an
	 * unnecessarily short alsa-like latency.
	 */
	ba.fragsize = (uint32_t) -1;
	ba.tlength = (uint32_t) -1;
	ba.prebuf = (uint32_t) -1;
	ba.minreq = (uint32_t) -1;
	ba.maxlength = (uint32_t) -1;

	ss.channels = sound_params->channels;
	ss.rate = sound_params->rate;
	switch (sound_params->fmt) {
	case SFMT_U8:
		ss.format = PA_SAMPLE_U8;
		break;
	case SFMT_S16 | SFMT_LE:
		ss.format = PA_SAMPLE_S16LE;
		break;
	case SFMT_S16 | SFMT_BE:
		ss.format = PA_SAMPLE_S16BE;
		break;
	case SFMT_FLOAT:
	case SFMT_FLOAT | SFMT_LE:
		ss.format = PA_SAMPLE_FLOAT32LE;
		break;
	case SFMT_FLOAT | SFMT_BE:
		ss.format = PA_SAMPLE_FLOAT32BE;
		break;
	case SFMT_S32 | SFMT_LE:
		ss.format = PA_SAMPLE_S32LE;
		break;
	case SFMT_S32 | SFMT_BE:
		ss.format = PA_SAMPLE_S32BE;
		break;

	default:
		fatal ("pulse: got unrequested format");
	}

	debug ("opening stream");

	pa_threaded_mainloop_lock (mainloop);

	/* TODO: figure out if there are useful stream properties to set.
	 *
	 * I do not really see any in proplist.h that we can set from
	 * here (there are media title/artist/etc props but we do not
	 * have that data available here).
	 */
	s = pa_stream_new (context, "music", &ss, NULL);
	if (!s)
		fatal ("pulse: stream allocation failed");

	pa_stream_set_state_callback (s, stream_state_callback, mainloop);
	pa_stream_set_write_callback (s, stream_write_callback, mainloop);

	/* Ignore return value, rely on failed stream state instead. */
	pa_stream_connect_playback (
		s, NULL, &ba,
		PA_STREAM_INTERPOLATE_TIMING |
		PA_STREAM_AUTO_TIMING_UPDATE |
		PA_STREAM_ADJUST_LATENCY,
		NULL, NULL);

	while (1) {
		pa_stream_state_t state = pa_stream_get_state (s);

		if (state == PA_STREAM_READY)
			break;

		if (!PA_STREAM_IS_GOOD (state)) {
			error ("PulseAudio stream connection failed");

			goto fail;
		}

		debug ("waiting for stream to become ready...");
		pa_threaded_mainloop_wait (mainloop);
	}

	/* Only set the global stream now that it is actually ready */
	stream = s;

	pa_threaded_mainloop_unlock (mainloop);

	return 1;

fail:
	pa_stream_unref (s);

	pa_threaded_mainloop_unlock (mainloop);
	return 0;
}

static void pulse_close (void)
{
	debug ("closing stream");

	pa_threaded_mainloop_lock (mainloop);

	pa_stream_disconnect (stream);
	pa_stream_unref (stream);
	stream = NULL;

	pa_threaded_mainloop_unlock (mainloop);
}

static int pulse_play (const char *buff, const size_t size)
{
	size_t offset = 0;

	debug ("Got %d bytes to play", (int)size);

	pa_threaded_mainloop_lock (mainloop);

	/* The buffer is usually writable when we get here, and there
	 * are usually few (if any) writes after the first one. So
	 * there is no point in doing further writes directly from the
	 * callback: we can just do all writes from this thread.
	 */

	/* Break out of the loop if some other thread manages to close
	 * our stream underneath us.
	 */
	while (stream) {
		size_t towrite = MIN(pa_stream_writable_size (stream),
				     size - offset);
		debug ("writing %d bytes", (int)towrite);

		/* We have no working way of dealing with errors
		 * (see below). */
		if (pa_stream_write(stream, buff + offset, towrite,
				    NULL, 0, PA_SEEK_RELATIVE))
			error ("pa_stream_write failed");

		offset += towrite;

		if (offset >= size)
			break;

		pa_threaded_mainloop_wait (mainloop);
	}

	pa_threaded_mainloop_unlock (mainloop);

	debug ("Done playing!");

	/* We should always return size, calling code does not deal
	 * well with anything else. Only read the rest if you want to
	 * know why.
	 *
	 * The output buffer reader thread (out_buf.c:read_thread)
	 * repeatedly loads some 64k/0.1s of audio into a buffer on
	 * the stack, then calls audio_send_pcm repeatedly until this
	 * entire buffer has been processed (similar to the loop in
	 * this function). audio_send_pcm applies the softmixer and
	 * equalizer, then feeds the result to this function, passing
	 * through our return value.
	 *
	 * So if we return less than size the equalizer/softmixer is
	 * re-applied to the remaining data, which is silly. Also,
	 * audio_send_pcm checks for our return value being zero and
	 * calls fatal() if it is, so try to always process *some*
	 * data. Also, out_buf.c uses the return value of this
	 * function from the last run through its inner loop to update
	 * its time attribute, which means it will be interestingly
	 * off if that loop ran more than once.
	 *
	 * Oh, and alsa.c seems to think it can return -1 to indicate
	 * failure, which will cause out_buf.c to rewind its buffer
	 * (to before its start, usually).
	 */
	return size;
}

static void volume_cb (const pa_cvolume *v, void *userdata)
{
	int *result = userdata;

	if (v)
		*result = ceil(100.0 * pa_cvolume_avg (v) / PA_VOLUME_NORM);

	pa_threaded_mainloop_signal (mainloop, 0);
}

static void sink_volume_cb (pa_context *c ATTR_UNUSED,
			    const pa_sink_info *i, int eol ATTR_UNUSED,
			    void *userdata)
{
	volume_cb (i ? &i->volume : NULL, userdata);
}

static void sink_input_volume_cb (pa_context *c ATTR_UNUSED,
				  const pa_sink_input_info *i,
				  int eol ATTR_UNUSED,
				  void *userdata ATTR_UNUSED)
{
	volume_cb (i ? &i->volume : NULL, userdata);
}

static int pulse_read_mixer (void)
{
	pa_operation *op;
	int result = 0;

	debug ("read mixer");

	pa_threaded_mainloop_lock (mainloop);

	if (showing_sink_volume) {
		op = pa_context_get_sink_info_by_index (
				context, stream ? pa_stream_get_device_index (stream) : pa_default_sink_index,
				sink_volume_cb, &result);
	} else if (stream) {
		op = pa_context_get_sink_input_info (
				context, pa_stream_get_index (stream),
				sink_input_volume_cb, &result);
	}

	if (showing_sink_volume || stream) {
		while (pa_operation_get_state (op) == PA_OPERATION_RUNNING)
			pa_threaded_mainloop_wait (mainloop);

		pa_operation_unref (op);
	}

	pa_threaded_mainloop_unlock (mainloop);

	return result;
}

static void pulse_set_mixer (int vol)
{
	pa_cvolume v;
	pa_operation *op;

	/* Setting volume for one channel does the right thing. */
	pa_cvolume_set(&v, 1, vol * PA_VOLUME_NORM / 100);

	pa_threaded_mainloop_lock (mainloop);

	if (showing_sink_volume) {
		op = pa_context_set_sink_volume_by_index (
			context, stream ? pa_stream_get_device_index (stream) : pa_default_sink_index,
			&v, NULL, NULL);
	} else if (stream) {
		op = pa_context_set_sink_input_volume (
			context, pa_stream_get_index (stream),
			&v, NULL, NULL);

		pa_operation_unref (op);
	}

	pa_threaded_mainloop_unlock (mainloop);
}

static int pulse_get_buff_fill (void)
{
	/* This function is problematic. MOC uses it to for the "time
	 * remaining" in the UI, but calls it more than once per
	 * second (after each chunk of audio played, not for each
	 * playback time update). We have to be fairly accurate here
	 * for that time remaining to not jump weirdly. But PulseAudio
	 * cannot give us a 100% accurate value here, as it involves a
	 * server roundtrip. And if we call this a lot it suggests
	 * switching to a mode where the value is interpolated, making
	 * it presumably more inaccurate (see the flags we pass to
	 * pa_stream_connect_playback).
	 *
	 * MOC also contains what I believe to be a race: it calls
	 * audio_get_buff_fill "soon" (after playing the first chunk)
	 * after starting playback of the next song, at which point we
	 * still have part of the previous song buffered. This means
	 * our position into the new song is negative, which fails an
	 * assert (in out_buf.c:out_buf_time_get). There is no sane
	 * way for us to detect this condition. I believe no other
	 * backend triggers this because the assert sits after an
	 * implicit float -> int seconds conversion, which means we
	 * have to be off by at least an entire second to get a
	 * negative value, and none of the other backends have buffers
	 * that large (alsa buffers are supposedly a few 100 ms).
	 */
	pa_usec_t buffered_usecs = 0;
	int buffered_bytes = 0;

	pa_threaded_mainloop_lock (mainloop);

	/* Using pa_stream_get_timing_info and returning the distance
	 * between write_index and read_index would be more obvious,
	 * but because of how the result is actually used I believe
	 * using the latency value is slightly more correct, and it
	 * makes the following crash-avoidance hack more obvious.
	 */

	/* This function will frequently fail the first time we call
	 * it (pulse does not have the requested data yet). We ignore
	 * that and just return 0.
	 *
	 * Deal with stream being NULL too, just in case this is
	 * called in a racy fashion similar to how reset() is.
	 */
	if (stream &&
	    pa_stream_get_latency (stream, &buffered_usecs, NULL) >= 0) {
		/* Crash-avoidance HACK: floor our latency to at most
		 * 1 second. It is usually more, but reporting that at
		 * the start of playback crashes MOC, and we cannot
		 * sanely detect when reporting it is safe.
		 */
		if (buffered_usecs > 1000000)
			buffered_usecs = 1000000;

		buffered_bytes = pa_usec_to_bytes (
			buffered_usecs,
			pa_stream_get_sample_spec (stream));
	}

	pa_threaded_mainloop_unlock (mainloop);

	debug ("buffer fill: %d usec / %d bytes",
	       (int) buffered_usecs, (int) buffered_bytes);

	return buffered_bytes;
}

static void flush_callback (pa_stream *s ATTR_UNUSED, int success,
			    void *userdata)
{
	int *result = userdata;

	*result = success;

	pa_threaded_mainloop_signal (mainloop, 0);
}

static int pulse_reset (void)
{
	pa_operation *op;
	int result = 0;

	debug ("reset requested");

	pa_threaded_mainloop_lock (mainloop);

	/* We *should* have a stream here, but MOC is racy, so bulletproof */
	if (stream) {
		op = pa_stream_flush (stream, flush_callback, &result);

		while (pa_operation_get_state (op) == PA_OPERATION_RUNNING)
			pa_threaded_mainloop_wait (mainloop);

		pa_operation_unref (op);
	} else
		logit ("pulse_reset() called without a stream");

	pa_threaded_mainloop_unlock (mainloop);

	return result;
}

static int pulse_get_rate (void)
{
	/* This is called once right after open. Do not bother making
	 * this fast. */

	int result;

	pa_threaded_mainloop_lock (mainloop);

	if (stream)
		result = pa_stream_get_sample_spec (stream)->rate;
	else {
		error ("get_rate called without a stream");
		result = 0;
	}

	pa_threaded_mainloop_unlock (mainloop);

	return result;
}

static void pulse_toggle_mixer_channel (void)
{
	showing_sink_volume = !showing_sink_volume;
}

static void sink_name_cb (pa_context *c ATTR_UNUSED,
			  const pa_sink_info *i, int eol ATTR_UNUSED,
			  void *userdata)
{
	char **result = userdata;

	if (i && !*result)
		*result = xstrdup(pa_proplist_gets(i->proplist, PA_PROP_DEVICE_DESCRIPTION));

	pa_threaded_mainloop_signal (mainloop, 0);
}

static char *pulse_get_mixer_channel_name (void)
{
	char *result = NULL;

	pa_threaded_mainloop_lock (mainloop);

	if (showing_sink_volume) {
		pa_operation *op;
		op = pa_context_get_sink_info_by_index (
				context, stream ? pa_stream_get_device_index (stream) : pa_default_sink_index,
				sink_name_cb, &result);

		while (pa_operation_get_state (op) == PA_OPERATION_RUNNING)
			pa_threaded_mainloop_wait (mainloop);

		pa_operation_unref (op);
	} else {
		//result = xstrdup(PACKAGE_NAME);
		result = xstrdup("PulseStream");

	}

	pa_threaded_mainloop_unlock (mainloop);

	if (!result)
		result = xstrdup ("disconnected");

	return result;
}

void pulse_funcs (struct hw_funcs *funcs)
{
	funcs->init = pulse_init;
	funcs->shutdown = pulse_shutdown;
	funcs->open = pulse_open;
	funcs->close = pulse_close;
	funcs->play = pulse_play;
	funcs->read_mixer = pulse_read_mixer;
	funcs->set_mixer = pulse_set_mixer;
	funcs->get_buff_fill = pulse_get_buff_fill;
	funcs->reset = pulse_reset;
	funcs->get_rate = pulse_get_rate;
	funcs->toggle_mixer_channel = pulse_toggle_mixer_channel;
	funcs->get_mixer_channel_name = pulse_get_mixer_channel_name;
}
