/*
 * MOC - music on console
 * Copyright (C) 2009-2014 Ondřej Svoboda <ondrej@svobodasoft.cz>
 *
 * MPRIS (Media Player Remote Interfacing Specification) 1.0 implementation.
 * This is a D-Bus interface to MOC.
 * All processing is done in a separate server thread.
 *
 * TODO:
 * Almost no error-checking during message processing will be coded until it is
 * declared necessary. Errors are said to only emerge when there is
 * not enough memory. In that case the thread will stop looping anyway (I hope).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <dbus/dbus.h>

#include "audio.h"
#include "common.h"
#include "files.h"
#include "log.h"
#include "mpris.h"
#include "mpris_introspection.h"
#include "options.h"
#include "player.h"
#include "playlist.h"
#include "protocol.h"
#include "server.h"
#include "tags_cache.h"

static DBusConnection *dbus_conn; /* Connection handle. */
static DBusError dbus_err;        /* Error flag. */
static DBusMessage *msg;          /* An incoming message. */
static DBusMessageIter args_in, args_out;    /* Iterators for in- or outcoming messages' content. */
static DBusMessageIter array, dict, variant; /* Iterators for containers only used in outgoing messages. */

/* Shared with audio.c */
/* Track number in the current playlist.
 * It is not what we want when the playlist is shuffled.
 * TODO: Get the right track number. */
extern int curr_playing;
extern struct plist *curr_plist;
extern pthread_mutex_t curr_playing_mtx;
extern pthread_mutex_t plist_mtx;

/* Shared with server.c */
/* TODO: Do we need mutex for these? */
extern int server_quit;
extern struct tags_cache *tags_cache;

/* Flags to be changed by the server + their mutex. */
static int mpris_track_changed = 0;
static int mpris_status_changed = 0;
static int mpris_caps_changed = 0;
static int mpris_tracklist_changed = 0;
static pthread_mutex_t mpris_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Connection to D-Bus happens here. If it fails, for any reason, we just move
 * on as the MPRIS/D-Bus feature is not crucial for the server.
 * Later, it could be possible to rerun the initialization and the thread
 * on-demand or automatically after a period of time. */
void mpris_init()
{
	dbus_error_init(&dbus_err);

	dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &dbus_err);
	if (dbus_error_is_set(&dbus_err)) {
		logit("Error while connecting to D-Bus: %s", dbus_err.message);
		dbus_error_free(&dbus_err);
		return;
	}

	if (dbus_conn == NULL) {
		logit("Connection to D-Bus not established.");
		return;
	}

	int ret = dbus_bus_request_name(dbus_conn, MPRIS_BUS_NAME,
	DBUS_NAME_FLAG_DO_NOT_QUEUE, &dbus_err);
	if (dbus_error_is_set(&dbus_err)) {
		logit("Error while requesting a bus name: %s", dbus_err.message);
		dbus_error_free(&dbus_err);
		return;
	}

	if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		logit("Could not become the primary owner of the bus name.");
		return;
	}

	logit("Everything went fine when connecting to D-Bus.");
}

/* Low-level wrappers for sending message arguments with possible error checking. */

static void msg_add_string(char **value)
{
	if (!dbus_message_iter_append_basic(&args_out, DBUS_TYPE_STRING, value)) {
		logit("Out of memory!");
		/* Error handling would go here. */
	}
}

static void msg_add_int32(int *value)
{
	dbus_message_iter_append_basic(&args_out, DBUS_TYPE_INT32, value);
}

static void msg_add_uint16(unsigned short int *value)
{
	dbus_message_iter_append_basic(&args_out, DBUS_TYPE_UINT16, value);
}

/* Helper functions for sending bigger chunks of data or with some added logic. */

static void mpris_send_tracklist_length()
{
	LOCK(plist_mtx);
	int length = curr_plist != NULL ? plist_count(curr_plist) : 0;
	UNLOCK(plist_mtx);
	msg_add_int32(&length);
}

static void mpris_send_metadata_string_field(char **key, char **value)
{
	dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, 0, &dict);
		dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, key);
		dbus_message_iter_open_container(&dict, DBUS_TYPE_VARIANT, "s", &variant);
			dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, value);
		dbus_message_iter_close_container(&dict, &variant);
	dbus_message_iter_close_container(&array, &dict);
}

/* TODO: If tags are missing, at least a title made from file name should be returned! */
static void mpris_send_metadata(int item)
{
	char *location_key = "location";
	char *title_key = "title";
	char *artist_key = "artist";
	char *location_val = NULL;
	char *file = NULL;
	struct file_tags *tags;

	if (item >= 0) {
		LOCK(plist_mtx);
		file = plist_get_file(curr_plist, item);
		UNLOCK(plist_mtx);
	}
	if (file == NULL) {
		file = xstrdup("");
		tags = tags_new();
	} else {
		tags = tags_cache_get_immediate(tags_cache, file, TAGS_COMMENTS | TAGS_TIME);
	}

	if (!is_url(file)) {
		location_val = (char *)xmalloc(sizeof(char) * (7 + strlen(file) + 1));
		strcpy(location_val, "file://");
		strcat(location_val, file);
	} else {
		location_val = xstrdup(file);
	}
	free(file);

	dbus_message_iter_open_container(&args_out, DBUS_TYPE_ARRAY, "{sv}", &array);
	mpris_send_metadata_string_field(&location_key, &location_val);
	if (tags->title != NULL)
		mpris_send_metadata_string_field(&title_key, &tags->title);
	if (tags->artist != NULL)
		mpris_send_metadata_string_field(&artist_key, &tags->artist);

	dbus_message_iter_close_container(&args_out, &array);

	free(location_val);
	tags_free(tags);
}

static void mpris_send_status()
{
	DBusMessageIter structure;
	int state, shuffle, repeat_current, next, repeat;

	switch (audio_get_state()) {
		case STATE_PLAY:
			state = 0;
			break;
		case STATE_PAUSE:
			state = 1;
			break;
		case STATE_STOP:
			state = 2;
	}

	shuffle = options_get_bool("Shuffle");
	repeat  = options_get_bool("Repeat");
	next	= options_get_bool("AutoNext");
	repeat_current = !next && repeat;

	/* I saw a nice usage of a C++ << operator for appending values to the message. */
	dbus_message_iter_open_container(&args_out, DBUS_TYPE_STRUCT, 0, &structure);
		dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &state);
		dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &shuffle);
		dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &repeat_current);
		dbus_message_iter_append_basic(&structure, DBUS_TYPE_INT32, &repeat);
	dbus_message_iter_close_container(&args_out, &structure);
}

/* TODO: stub */
static void mpris_send_caps()
{
	int caps = 0
	| CAN_HAS_TRACKLIST			/* Yes, MOC always has a playlist, be it an actual playlist or a directory. */
	;

	msg_add_int32(&caps);
}

/* Functions for sending signals. */

static void mpris_tracklist_change_signal()
{
	msg = dbus_message_new_signal("/TrackList", MPRIS_IFACE, "TrackListChange");
	dbus_message_iter_init_append(msg, &args_out);

	mpris_send_tracklist_length();

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_connection_flush(dbus_conn);

	dbus_message_unref(msg);
}

static void mpris_track_change_signal()
{
	msg = dbus_message_new_signal("/Player", MPRIS_IFACE, "TrackChange");
	dbus_message_iter_init_append(msg, &args_out);

	LOCK(curr_playing_mtx);
	int curr = curr_playing;
	UNLOCK(curr_playing_mtx);
	mpris_send_metadata(curr);

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_connection_flush(dbus_conn);

	dbus_message_unref(msg);
}

static void mpris_status_change_signal()
{
	msg = dbus_message_new_signal("/Player", MPRIS_IFACE, "StatusChange");
	dbus_message_iter_init_append(msg, &args_out);

	mpris_send_status();

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_connection_flush(dbus_conn);

	dbus_message_unref(msg);
}

static void mpris_caps_change_signal()
{
	msg = dbus_message_new_signal("/Player", MPRIS_IFACE, "CapsChange");
	dbus_message_iter_init_append(msg, &args_out);

	mpris_send_caps();

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_connection_flush(dbus_conn);

	dbus_message_unref(msg);
}

/* Argument checking for incoming messages. */

static int mpris_arg_bool()
{
	return dbus_message_iter_init(msg, &args_in) &&
	       dbus_message_iter_get_arg_type(&args_in) == DBUS_TYPE_BOOLEAN;
}

static int mpris_arg_int32()
{
	return dbus_message_iter_init(msg, &args_in) &&
	       dbus_message_iter_get_arg_type(&args_in) == DBUS_TYPE_INT32;
}

/* Implementation of D-Bus methods (incomplete, see comments). */

static void mpris_root_methods()
{
	if (dbus_message_is_method_call(msg, INTROSPECTION_IFACE, "Introspect")) {
		msg_add_string(&root_introspection);
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Quit")) {
		server_quit = 1; /* TODO: Why care about using mutex? */
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Identity")) {
		char *identity = xstrdup(PACKAGE_STRING);
		msg_add_string(&identity);
		free(identity);
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "MprisVersion")) {
		short unsigned int version_major = 1, version_minor = 0;
		msg_add_uint16(&version_major);
		msg_add_uint16(&version_minor);
	}
}

static void mpris_tracklist_methods()
{
	if (dbus_message_is_method_call(msg, INTROSPECTION_IFACE, "Introspect")) {
		msg_add_string(&tracklist_introspection);
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "GetMetadata")) {
		if (mpris_arg_int32()) {
			int item;
			dbus_message_iter_get_basic(&args_in, &item);
			mpris_send_metadata(item);
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "GetCurrentTrack")) {
		/* In curr_playing there's not a number we always want. */
		LOCK(curr_playing_mtx);
		int curr = curr_playing != -1 ? curr_playing : 0;
		UNLOCK(curr_playing_mtx);
		msg_add_int32(&curr);
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "GetLength")) {
		mpris_send_tracklist_length();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "AddTrack")) {
		/*
		 * Not implemented yet!
		 * There are some issues to handle:
		 * - every client can have its own playlist, which one to add a file to then?
		 * - to add a file we seem to need to duplicate a few functions, such as
		 *   add_file_plist(), play_from_url(), add_url_to_plist()
		 *   from interface.c and code chunks processing the CMD_CLI_PLIST_ADD command
		 *   in server.c
		 */
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "DelTrack")) {
		/* Not implemented yet for the same reason as for AddTrack. */
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "SetLoop")) {
		if (mpris_arg_bool()) {
			int loop_plist;
			dbus_message_iter_get_basic(&args_in, &loop_plist);
			options_set_int("Repeat", loop_plist);
			/* We are asked to loop the playlist, so make it play more than one file. */
			if (loop_plist) options_set_int("AutoNext", 1);
			add_event_all(EV_OPTIONS, NULL);
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "SetRandom")) {
		if (mpris_arg_bool()) {
			int random;
			dbus_message_iter_get_basic(&args_in, &random);
			options_set_int("Shuffle", random);
			add_event_all(EV_OPTIONS, NULL);
		}
	}
}

static void mpris_player_methods()
{
	if (dbus_message_is_method_call(msg, INTROSPECTION_IFACE, "Introspect")) {
		msg_add_string(&player_introspection);
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Next")) {
		audio_next();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Prev")) {
		audio_prev();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Pause")) {
		switch (audio_get_state()) {
			case STATE_PAUSE:
				audio_unpause();
				break;
			case STATE_PLAY:
				audio_pause();
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Stop")) {
		audio_stop();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Play")) {
		/* TODO: This does not make MOC play a song if it hasn't played one yet! */
		char *file;
		switch (audio_get_state()) {
			case STATE_PAUSE:
				audio_unpause();
				break;
			case STATE_PLAY:
				LOCK(plist_mtx);
				LOCK(curr_playing_mtx);
				if (curr_plist != NULL && curr_playing != -1) {
					file = plist_get_file (curr_plist, curr_playing);
					UNLOCK(plist_mtx);
					UNLOCK(curr_playing_mtx);
					audio_play(file);
					free(file);
				} else {
					UNLOCK(plist_mtx);
					UNLOCK(curr_playing_mtx);
				}
				break;
			case STATE_STOP:
				audio_play("");
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "Repeat")) {
		if (mpris_arg_bool()) {
			int repeat_current;
			dbus_message_iter_get_basic(&args_in, &repeat_current);
			options_set_int("Repeat", repeat_current);
			options_set_int("AutoNext", !repeat_current);
			add_event_all(EV_OPTIONS, NULL);
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "GetStatus")) {
		mpris_send_status();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "GetMetadata")) {
		LOCK(curr_playing_mtx);
		int curr = curr_playing;
		UNLOCK(curr_playing_mtx);
		mpris_send_metadata(curr);
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "GetCaps")) {
		mpris_send_caps();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "VolumeSet")) {
		if (mpris_arg_int32()) {
			int volume;
			dbus_message_iter_get_basic(&args_in, &volume);
			/* TODO: Check the parameter first! */
			audio_set_mixer(volume);
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "VolumeGet")) {
		int volume = audio_get_mixer();
		msg_add_int32(&volume);
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "PositionSet")) {
		/* TODO: Should support milisecond precision too */
		if (mpris_arg_int32()) {
			int pos;
			dbus_message_iter_get_basic(&args_in, &pos);
			/* TODO: Do we need to check the value for correctness? */
			audio_jump_to(pos / 1000);
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE, "PositionGet")) {
		int pos = audio_get_time() * 1000;
		msg_add_int32(&pos);
	}
}

/* A server thread where all D-Bus messages are received and signals are sent. */
void *mpris_thread(void *unused ATTR_UNUSED)
{
	DBusMessage *reply;
	const char *path; /* The path an incoming message has been sent to. */

	/* If no D-Bus connection has been established we have nothing to do. */
	if (dbus_conn == NULL) return NULL;

	logit("Starting the MPRIS thread.");

	/* Wait for an incoming message for a max. of MPRIS_TIMEOUT (50 ms). */
	while (dbus_connection_read_write(dbus_conn, MPRIS_TIMEOUT)) {
		if (server_quit) {
			logit("Stopping the MPRIS thread due to server exit.");
			return NULL;
		}

		/* Send signals if necessary. */
		LOCK(mpris_mutex);
		if (mpris_tracklist_changed) {
			mpris_tracklist_change_signal();
			mpris_tracklist_changed = 0;
		}
		if (mpris_track_changed) {
			mpris_track_change_signal();
			mpris_track_changed = 0;
		}
		if (mpris_caps_changed) {
			mpris_caps_change_signal();
			mpris_caps_changed = 0;
		}
		if (mpris_status_changed) {
			mpris_status_change_signal();
			mpris_status_changed = 0;
		}
		UNLOCK(mpris_mutex);

		/* Fetch an incoming message. */
		msg = dbus_connection_pop_message(dbus_conn);
		if (msg == NULL) continue;

		/* Respond to all messages. They say in the specification some messages
		 * are sometimes no-ops, so this is the place to start moving code. */

		reply = dbus_message_new_method_return(msg);
		dbus_message_iter_init_append(reply, &args_out);

		/* Process the incoming message and prepare a response. */
		if ((path = dbus_message_get_path(msg)) != NULL) {
			logit("MPRIS msg path %s", path);
			if (!strcmp("/", path))
				mpris_root_methods();
			else if (!strcmp("/Player", path))
				mpris_player_methods();
			else if (!strcmp("/TrackList", path))
				mpris_tracklist_methods();
		}

		/* Send the response and clean up. */
		dbus_connection_send(dbus_conn, reply, NULL);
		dbus_message_unref(msg);
		dbus_connection_flush(dbus_conn);
	}

	logit("Stopping MPRIS thread due to a loss of communication with D-Bus.");
	return NULL;
}

/* Hooks in the core/server. */

/* Not used yet. */
void mpris_tracklist_change()
{
	LOCK(mpris_mutex);
	mpris_tracklist_changed = 1;
	UNLOCK(mpris_mutex);
}

void mpris_track_change()
{
	LOCK(mpris_mutex);
	mpris_track_changed = 1;
	UNLOCK(mpris_mutex);
}

void mpris_status_change()
{
	LOCK(mpris_mutex);
	mpris_status_changed = 1;
	UNLOCK(mpris_mutex);
}

/* Not used yet. */
void mpris_caps_change()
{
	LOCK(mpris_mutex);
	mpris_caps_changed = 1;
	UNLOCK(mpris_mutex);
}

void mpris_exit()
{
	pthread_mutex_destroy(&mpris_mutex);
}
