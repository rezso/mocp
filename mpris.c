/*
 * MOC - music on console
 * Copyright (C) 2009-2014 Ondřej Svoboda <ondrej@svobodasoft.cz>
 *
 * MPRIS (Media Player Remote Interfacing Specification) 2.0 implementation.
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
static DBusMessage* msg_error;    /* Error message. */


/* variables for storing data appended to messages */
static dbus_bool_t F = 0;
static dbus_bool_t T = 1;
static char* val_s;
static dbus_bool_t val_b;
static double val_d;
static dbus_int64_t val_x;

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
static bool mpris_track_changed = 0;
static bool mpris_status_changed = 0;
static bool mpris_caps_changed = 0;
static bool mpris_tracklist_changed = 0;
static bool mpris_seeked = 0;
pthread_mutex_t mpris_mutex = PTHREAD_MUTEX_INITIALIZER;

static const int MPRIS_TIMEOUT =			50;
static const char* MPRIS_BUS_NAME =			"org.mpris.MediaPlayer2.moc";
static const char* MPRIS_OBJECT =			"/org/mpris/MediaPlayer2";
static const char* MPRIS_IFACE_ROOT =		"org.mpris.MediaPlayer2";
static const char* MPRIS_IFACE_PLAYER =		"org.mpris.MediaPlayer2.Player";
// static const char* MPRIS_IFACE_TRACKLIST =	"org.mpris.MediaPlayer2.TrackList";
static const char* INTROSPECTION_IFACE =	"org.freedesktop.DBus.Introspectable";
static const char* PROPERTIES_IFACE =		"org.freedesktop.DBus.Properties";

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

	logit("Successfully connected to D-Bus.");
}

/* Low-level wrappers for sending message arguments with possible error checking. */

static void msg_add_string(char **value)
{
	if (!dbus_message_iter_append_basic(&args_out, DBUS_TYPE_STRING, value)) {
		logit("Out of memory!");
		/* Error handling would go here. */
	}
}

// static void msg_add_int32(int *value)
// {
// 	dbus_message_iter_append_basic(&args_out, DBUS_TYPE_INT32, value);
// }
//
// static void msg_add_uint16(unsigned short int *value)
// {
// 	dbus_message_iter_append_basic(&args_out, DBUS_TYPE_UINT16, value);
// }

static void msg_add_variant(DBusMessageIter *array, char type, void *value)
{
	DBusMessageIter variant;
	char type_as_string[2] = {type, 0};

	dbus_message_iter_open_container(array, DBUS_TYPE_VARIANT, type_as_string, &variant);
			dbus_message_iter_append_basic(&variant, type, value);
	dbus_message_iter_close_container(array, &variant);
}

static void msg_add_dict(DBusMessageIter *array, char type, char **key, void *value)
{
	DBusMessageIter dict;

	dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, NULL, &dict);
		dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, key);
		msg_add_variant(&dict, type, value);
	dbus_message_iter_close_container(array, &dict);
}

static void msg_add_dict_string_as_array(DBusMessageIter *array, char **key, char **value)
{
	DBusMessageIter dict;
	DBusMessageIter variant;
	DBusMessageIter array_str;

	dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, NULL, &dict);
		dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, key);
		dbus_message_iter_open_container(&dict, DBUS_TYPE_VARIANT, "as", &variant);
			dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array_str);
				dbus_message_iter_append_basic(&array_str, DBUS_TYPE_STRING, value);
			dbus_message_iter_close_container(&variant, &array_str);
		dbus_message_iter_close_container(&dict, &variant);
	dbus_message_iter_close_container(array, &dict);
}

static inline char* loop_status() {
	bool repeat_current, next, repeat;
	repeat = options_get_bool("Repeat");
	next = options_get_bool("AutoNext");
	repeat_current = !next && repeat;
	if (repeat_current)
		return "Track";
	else if (repeat)
		return "Playlist";
	else
		return "None";
}

static inline char* playback_status() {
	switch (audio_get_state()) {
		case STATE_PLAY:
			return "Playing";
		case STATE_PAUSE:
			return "Paused";
		case STATE_STOP:
		default:
			return "Stopped";
	}
}

static void msg_add_variant_playback_status(DBusMessageIter *array)
{
	val_s = playback_status();
	msg_add_variant(array, DBUS_TYPE_STRING, &val_s);
}

static void msg_add_dict_playback_status(DBusMessageIter *array)
{
	char* key = "PlaybackStatus";
	val_s = playback_status();
	msg_add_dict(array, DBUS_TYPE_STRING, &key, &val_s);
}

/* TODO: If tags are missing, at least a title made from file name should be returned! */
static void msg_add_variant_metadata(DBusMessageIter *array) {
	DBusMessageIter array_meta;
	DBusMessageIter variant;

	char* key;
	char *file;
	struct file_tags *tags;
	int curr;

	LOCK(curr_playing_mtx);
	curr = curr_playing;
	UNLOCK(curr_playing_mtx);
	if (curr < 0) {
		dbus_message_iter_open_container(array, DBUS_TYPE_VARIANT, "a{sv}", &variant);
			dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "{sv}", &array_meta);
			dbus_message_iter_close_container(&variant, &array_meta);
		dbus_message_iter_close_container(array, &variant);
		return;
	}

	LOCK(plist_mtx);
	file = plist_get_file(curr_plist, curr);
	UNLOCK(plist_mtx);

	if (file == NULL) {
		file = xstrdup("");
		tags = tags_new();
	} else {
		tags = tags_cache_get_immediate(tags_cache, file, TAGS_COMMENTS | TAGS_TIME);
	}

	dbus_message_iter_open_container(array, DBUS_TYPE_VARIANT, "a{sv}", &variant);
		dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "{sv}", &array_meta);
			key = "mpris:trackid";
			val_s = "moc/track/xxxx"; // TODO: not unique, not very conformant with specs
			msg_add_dict(&array_meta, DBUS_TYPE_STRING, &key, &val_s); // TODO: type o?
			key = "mpris:length";
			val_x = tags->time * 1000000;
			msg_add_dict(&array_meta, DBUS_TYPE_INT64, &key, &val_x);
			key = "xesam:title";
			if (tags->title)
				val_s = tags->title;
			else
				val_s = "[unknown title]";
			msg_add_dict(&array_meta, DBUS_TYPE_STRING, &key, &val_s);
			key = "xesam:artist";
			if (tags->artist)
				val_s = tags->artist;
			else
				val_s = "[unknown artist]";
			msg_add_dict_string_as_array(&array_meta, &key, &val_s);
			key = "xesam:album";
			if (tags->album)
				val_s = tags->album;
			else
				val_s = "[unknown album]";
			msg_add_dict(&array_meta, DBUS_TYPE_STRING, &key, &val_s);
			// TODO: Return mpris:artUrl if cover.jpg is present 

		dbus_message_iter_close_container(&variant, &array_meta);
	dbus_message_iter_close_container(array, &variant);

	free(file);
	tags_free(tags);
}

static void msg_add_dict_metadata(DBusMessageIter *array)
{
	DBusMessageIter dict;
	char* key = "Metadata";

	dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, NULL, &dict);
		dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &key);
		msg_add_variant_metadata(&dict);
	dbus_message_iter_close_container(array, &dict);
}

/* Functions for sending signals. */

// static void mpris_tracklist_change_signal()
// {
// 	msg = dbus_message_new_signal(MPRIS_OBJECT, MPRIS_IFACE_TRACKLIST, "TrackListChange");
// 	dbus_message_iter_init_append(msg, &args_out);
//
// 	mpris_send_tracklist_length();
//
// 	dbus_connection_send(dbus_conn, msg, NULL);
// 	dbus_connection_flush(dbus_conn);
//
// 	dbus_message_unref(msg);
// }

static void mpris_track_change_signal()
{
	DBusMessageIter array;

	debug("MPRIS Sending track change signal");
	msg = dbus_message_new_signal(MPRIS_OBJECT, PROPERTIES_IFACE, "PropertiesChanged");
	dbus_message_iter_init_append(msg, &args_out);
	dbus_message_iter_append_basic(&args_out, DBUS_TYPE_STRING, &MPRIS_IFACE_PLAYER);
	dbus_message_iter_open_container(&args_out, DBUS_TYPE_ARRAY, "{sv}", &array);
		msg_add_dict_metadata(&array);
		msg_add_dict_playback_status(&array);
	dbus_message_iter_close_container(&args_out, &array);
	dbus_message_iter_open_container(&args_out, DBUS_TYPE_ARRAY, "s", &array);
	dbus_message_iter_close_container(&args_out, &array);

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_connection_flush(dbus_conn);
	dbus_message_unref(msg);
}

static void mpris_seeked_signal()
{
	debug("MPRIS Sending position change signal");
	msg = dbus_message_new_signal(MPRIS_OBJECT, PROPERTIES_IFACE, "Seeked");
	dbus_message_iter_init_append(msg, &args_out);
	val_x = audio_get_time() * 1000000;
	dbus_message_iter_append_basic(&args_out, DBUS_TYPE_INT64, &val_x);

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_connection_flush(dbus_conn);
	dbus_message_unref(msg);
}

static void mpris_status_change_signal()
{
	msg = dbus_message_new_signal(MPRIS_OBJECT, MPRIS_IFACE_PLAYER, "StatusChange");
	dbus_message_iter_init_append(msg, &args_out);

// 	mpris_send_status();

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_connection_flush(dbus_conn);

	dbus_message_unref(msg);
}

// static void mpris_caps_change_signal()
// {
// 	msg = dbus_message_new_signal(MPRIS_OBJECT, MPRIS_IFACE_PLAYER, "CapsChange");
// 	dbus_message_iter_init_append(msg, &args_out);
// 
// 	mpris_send_caps();
// 
// 	dbus_connection_send(dbus_conn, msg, NULL);
// 	dbus_connection_flush(dbus_conn);
// 
// 	dbus_message_unref(msg);
// }

/* Argument checking for incoming messages. */

static int mpris_arg_bool()
{
	return dbus_message_iter_get_arg_type(&args_in) == DBUS_TYPE_BOOLEAN;
}

static int mpris_arg_int32()
{
	return dbus_message_iter_get_arg_type(&args_in) == DBUS_TYPE_INT32;
}

static int mpris_arg_int64()
{
	return dbus_message_iter_get_arg_type(&args_in) == DBUS_TYPE_INT64;
}

/* Implementation of D-Bus methods (incomplete, see comments). */

static void mpris_root_methods()
{
	debug("MPRIS root method");
	if (dbus_message_is_method_call(msg, MPRIS_IFACE_ROOT, "Quit")) {
		server_quit = 1; /* TODO: Why care about using mutex? */
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_ROOT, "Raise")) {
		/* We can't raise MOC, so we ignore it */
	}
}

// static void mpris_tracklist_methods()
// {
// 	debug("MPRIS tracklist method");
// 	if (dbus_message_is_method_call(msg, MPRIS_IFACE_TRACKLIST, "GetMetadata")) {
// 		if (mpris_arg_int32()) {
// 			int item;
// 			dbus_message_iter_get_basic(&args_in, &item);
// // 			mpris_send_metadata(item);
// 		}
// 	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_TRACKLIST, "GetCurrentTrack")) {
// 		/* In curr_playing there's not a number we always want. */
// 		LOCK(curr_playing_mtx);
// 		int curr = curr_playing != -1 ? curr_playing : 0;
// 		UNLOCK(curr_playing_mtx);
// 		msg_add_int32(&curr);
// 	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_TRACKLIST, "GetLength")) {
// 		mpris_send_tracklist_length();
// 	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_TRACKLIST, "AddTrack")) {
// 		/*
// 		 * Not implemented yet!
// 		 * There are some issues to handle:
// 		 * - every client can have its own playlist, which one to add a file to then?
// 		 * - to add a file we seem to need to duplicate a few functions, such as
// 		 *   add_file_plist(), play_from_url(), add_url_to_plist()
// 		 *   from ifaceace.c and code chunks processing the CMD_CLI_PLIST_ADD command
// 		 *   in server.c
// 		 */
// 	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_TRACKLIST, "DelTrack")) {
// 		/* Not implemented yet for the same reason as for AddTrack. */
// 	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_TRACKLIST, "SetLoop")) {
// 		if (mpris_arg_bool()) {
// 			int loop_plist;
// 			dbus_message_iter_get_basic(&args_in, &loop_plist);
// 			options_set_int("Repeat", loop_plist);
// 			/* We are asked to loop the playlist, so make it play more than one file. */
// 			if (loop_plist) options_set_int("AutoNext", 1);
// 			add_event_all(EV_OPTIONS, NULL);
// 		}
// 	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_TRACKLIST, "SetRandom")) {
// 		if (mpris_arg_bool()) {
// 			int random;
// 			dbus_message_iter_get_basic(&args_in, &random);
// 			options_set_int("Shuffle", random);
// 			add_event_all(EV_OPTIONS, NULL);
// 		}
// 	}
// }

static void mpris_player_methods()
{
	debug("MPRIS player method");
	if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "Next")) {
		audio_next();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "Previous")) {
		audio_prev();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "PlayPause")) {
		switch (audio_get_state()) {
			case STATE_PAUSE:
				audio_unpause();
				break;
			case STATE_PLAY:
				audio_pause();
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "Pause")) {
		audio_pause();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "Stop")) {
		audio_stop();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "Play")) {
		if (audio_get_state() == STATE_PAUSE)
			audio_unpause();
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "SetPosition")) {
		if (mpris_arg_int64()) {
			dbus_int64_t pos;
			dbus_message_iter_next(&args_in); // TODO: ignoring TrackId
			dbus_message_iter_get_basic(&args_in, &pos);
			/* TODO: Per specification, should position beyond the end of the track should act as "next" */
			if (pos > 0)
				audio_jump_to(pos / 1000000);
		}
	} else if (dbus_message_is_method_call(msg, MPRIS_IFACE_PLAYER, "Seek")) {
		if (mpris_arg_int64()) {
			dbus_int64_t pos;
			dbus_message_iter_get_basic(&args_in, &pos);
			/* TODO: Per specification, should position beyond the end of the track should act as "next" and negative beyond the start should seek to the beginning */
			audio_seek(pos / 1000000);
		}
	}
}

static void mpris_properties_getall_root() {
	char* key;
	DBusMessageIter array;

	dbus_message_iter_open_container(&args_out, DBUS_TYPE_ARRAY, "{sv}", &array);

	key = "Identity";
	val_s = xstrdup(PACKAGE_NAME);
	msg_add_dict(&array, DBUS_TYPE_STRING, &key, &val_s);
	key = "CanQuit";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &T);
	key = "CanRaise";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &F);
	key = "HasTrackList";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &F); // TODO: T
	key = "SupportedUriSchemes";
	val_s = "file"; // TODO: add other URI schemes
	msg_add_dict_string_as_array(&array, &key, &val_s);
	key = "SupportedMimeTypes";
	val_s = "application/ogg"; // TODO: add other Mime types
	msg_add_dict_string_as_array(&array, &key, &val_s);
	key = "DesktopEntry";
	val_s = "moc";
	msg_add_dict(&array, DBUS_TYPE_STRING, &key, &val_s);

	dbus_message_iter_close_container(&args_out, &array);
}

static void mpris_properties_get_root(char* key) {
	if (!strcmp("Identity", key)) {
		val_s = xstrdup(PACKAGE_NAME);
		msg_add_variant(&args_out, DBUS_TYPE_STRING, &val_s);
	} else if (!strcmp("CanQuit", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &T);
	} else if (!strcmp("CanRaise", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &F);
	} else if (!strcmp("HasTrackList", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &F); // TODO: T
	} else if (!strcmp("SupportedUriSchemes", key)) {
		val_s = "file"; // TODO: add other URI schemes
		msg_add_variant(&args_out, DBUS_TYPE_STRING, &val_s);
	} else if (!strcmp("SupportedMimeTypes", key)) {
		val_s = "application/ogg"; // TODO: add other Mime types
		msg_add_variant(&args_out, DBUS_TYPE_STRING, &val_s);
	} else if (!strcmp("DesktopEntry", key)) {
		val_s = "moc";
		msg_add_variant(&args_out, DBUS_TYPE_STRING, &val_s);
	} else {
		logit("MPRIS Get unknown property: %s", key);
		msg_error = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "No such interface");
		dbus_connection_send(dbus_conn, msg_error, NULL);
		dbus_connection_flush(dbus_conn);
	}
}

static void mpris_properties_getall_player() {
	char* key;
	DBusMessageIter array;

	dbus_message_iter_open_container(&args_out, DBUS_TYPE_ARRAY, "{sv}", &array);
	key = "Rate";
	val_d = 1;
	msg_add_dict(&array, DBUS_TYPE_DOUBLE, &key, &val_d);
	key = "MinimumRate";
	msg_add_dict(&array, DBUS_TYPE_DOUBLE, &key, &val_d);
	key = "MaximumRate";
	msg_add_dict(&array, DBUS_TYPE_DOUBLE, &key, &val_d);
	key = "Volume";
	val_d = audio_get_mixer() / 100.0;
	msg_add_dict(&array, DBUS_TYPE_DOUBLE, &key, &val_d);
	key = "Position";
	val_x = audio_get_time() * 1000000;
	msg_add_dict(&array, DBUS_TYPE_INT64, &key, &val_x);
	key = "CanGoNext";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &T);
	key = "CanGoPrevious";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &T);
	key = "CanPlay";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &T);
	key = "CanPause";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &T);
	key = "CanSeek";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &T);
	key = "CanControl";
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &T);
	key = "LoopStatus";
	val_s = loop_status();
	msg_add_dict(&array, DBUS_TYPE_STRING, &key, &val_s);
	key = "Shuffle";
	val_b = options_get_bool("Shuffle");
	msg_add_dict(&array, DBUS_TYPE_BOOLEAN, &key, &val_b);

	msg_add_dict_playback_status(&array);
	msg_add_dict_metadata(&array);

	dbus_message_iter_close_container(&args_out, &array);
}

static void mpris_properties_get_player(char* key) {
	if (!strcmp("Rate", key)) {
		val_d = 1;
		msg_add_variant(&args_out, DBUS_TYPE_DOUBLE, &val_d);
	} else if (!strcmp("MinimumRate", key)) {
		val_d = 1;
		msg_add_variant(&args_out, DBUS_TYPE_DOUBLE, &val_d);
	} else if (!strcmp("MaximumRate", key)) {
		val_d = 1;
		msg_add_variant(&args_out, DBUS_TYPE_DOUBLE, &val_d);
	} else if (!strcmp("Volume", key)) {
		val_d = audio_get_mixer() / 100.0;
		msg_add_variant(&args_out, DBUS_TYPE_DOUBLE, &val_d);
	} else if (!strcmp("Position", key)) {
		val_x = audio_get_time() * 1000000;
		msg_add_variant(&args_out, DBUS_TYPE_INT64, &val_x);
	} else if (!strcmp("CanGoNext", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &T);
	} else if (!strcmp("CanGoPrevious", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &T);
	} else if (!strcmp("CanPlay", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &T);
	} else if (!strcmp("CanPause", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &T);
	} else if (!strcmp("CanSeek", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &T);
	} else if (!strcmp("CanControl", key)) {
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &T);
	} else if (!strcmp("LoopStatus", key)) {
		val_s = loop_status();
		msg_add_variant(&args_out, DBUS_TYPE_STRING, &val_s);
	} else if (!strcmp("Shuffle", key)) {
		val_b = options_get_bool("Shuffle");
		msg_add_variant(&args_out, DBUS_TYPE_BOOLEAN, &val_b);
	} else if (!strcmp("PlaybackStatus", key)) {
		msg_add_variant_playback_status(&args_out);
	} else if (!strcmp("Metadata", key)) {
		msg_add_variant_metadata(&args_out);
	} else {
		logit("MPRIS Get unknown property: %s", key);
		msg_error = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "No such interface");
		dbus_connection_send(dbus_conn, msg_error, NULL);
		dbus_connection_flush(dbus_conn);
	}
}

static void mpris_properties_set_player(char* key, char type, void* value) {
	if (!strcmp("Rate", key)) {
		if (type != DBUS_TYPE_DOUBLE) goto err;
		if (*(double*)value != 1) {
			logit("MPRIS Can't set rate to a value different than 1");
			goto err;
		}
	} else if (!strcmp("LoopStatus", key)) {
		if (type == 's') {
			char* str = *(char**)value;
			if (!strcmp(str, "None")) {
				options_set_bool("Repeat", 0);
				options_set_bool("AutoNext", 1);
			} else if (!strcmp(str, "Track")) {
				options_set_bool("Repeat", 1);
				options_set_bool("AutoNext", 0);
			} else if (!strcmp(str, "Playlist")) {
				options_set_bool("Repeat", 1);
				options_set_bool("AutoNext", 1);
			} else {
				logit("MPRIS Can't set unknown LoopStatus: %s", str);
				goto err;
			}
		}
	} else if (!strcmp("Shuffle", key)) {
		if (type != DBUS_TYPE_BOOLEAN) goto err;
		options_set_bool("Shuffle", *(dbus_bool_t*)value);
	} else if (!strcmp("Volume", key)) {
		if (type != DBUS_TYPE_DOUBLE) goto err;
		double vol = *(double*)value;
		vol = CLAMP(0, vol, 1);
		audio_set_mixer(vol * 100);
	}

	if (0) {
			err:
				logit("MPRIS Set property: incorrect arguments");
				msg_error = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Invalid arguments for Set method");
				dbus_connection_send(dbus_conn, msg_error, NULL);
				dbus_connection_flush(dbus_conn);
	}
}

static void mpris_properties() {
	if (dbus_message_is_method_call(msg, PROPERTIES_IFACE, "GetAll")) {
		char* iface;
		dbus_message_get_args(msg, &dbus_err, DBUS_TYPE_STRING, &iface);
		logit("MPRIS GetAll properties for interface: %s", iface);

		if (!strcmp(MPRIS_IFACE_ROOT, iface)) {
			mpris_properties_getall_root();
		} else if (!strcmp(MPRIS_IFACE_PLAYER, iface)) {
			mpris_properties_getall_player();
// 		} else if (!strcmp(MPRIS_IFACE_TRACKLIST, str)) {
		} else {
			logit("MPRIS GetAll properties for unknown interface: %s", iface);
			msg_error = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, "No such interface");
			dbus_connection_send(dbus_conn, msg_error, NULL);
			dbus_connection_flush(dbus_conn);
		}
	} else if (dbus_message_is_method_call(msg, PROPERTIES_IFACE, "Get")) {
		char* iface;
		char* key;

		dbus_message_get_args(msg, &dbus_err, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &key);
		logit("MPRIS Get property: %s", key);

		if (!strcmp(MPRIS_IFACE_ROOT, iface)) {
			mpris_properties_get_root(key);
		} else if (!strcmp(MPRIS_IFACE_PLAYER, iface)) {
			mpris_properties_get_player(key);
// 		} else if (!strcmp(MPRIS_IFACE_TRACKLIST, str)) {
		} else {
			logit("MPRIS Get properties for unknown interface: %s", iface);
			msg_error = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, "No such interface");
			dbus_connection_send(dbus_conn, msg_error, NULL);
			dbus_connection_flush(dbus_conn);
		}
	} else if (dbus_message_is_method_call(msg, PROPERTIES_IFACE, "Set")) {
		char* iface;
		char* key;
		char type;
		DBusBasicValue value;

		DBusMessageIter args;
		DBusMessageIter variant;

		dbus_message_iter_init(msg, &args);
		if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) goto err;
		dbus_message_iter_get_basic(&args, &iface);
		if (!dbus_message_iter_next(&args)) goto err;
		if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) goto err;
		dbus_message_iter_get_basic(&args, &key);
		if (!dbus_message_iter_next(&args)) goto err;
		if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT) goto err;
		dbus_message_iter_recurse(&args, &variant);
		type = dbus_message_iter_get_arg_type(&variant);
		dbus_message_iter_get_basic(&variant, &value);

		logit("MPRIS Set property: %s, value of type: %c", key, type);

		if (!strcmp(MPRIS_IFACE_ROOT, iface)) {
			logit("MPRIS No properties to set on %s", iface);
			msg_error = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "No property to set");
			dbus_connection_send(dbus_conn, msg_error, NULL);
			dbus_connection_flush(dbus_conn);
		} else if (!strcmp(MPRIS_IFACE_PLAYER, iface)) {
			mpris_properties_set_player(key, type, &value);
// 		} else if (!strcmp(MPRIS_IFACE_TRACKLIST, iface)) {
		} else {
			logit("MPRIS Set property for unknown interface: %s", iface);
			msg_error = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, "No such interface");
			dbus_connection_send(dbus_conn, msg_error, NULL);
			dbus_connection_flush(dbus_conn);
		}

		if (0) {
			err:
				logit("MPRIS Set property: incorrect arguments");
				msg_error = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Invalid arguments for Set method");
				dbus_connection_send(dbus_conn, msg_error, NULL);
				dbus_connection_flush(dbus_conn);
		}
	} else {
			logit("MPRIS unknown method: %s", dbus_message_get_member(msg));
			msg_error = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "No such method");
			dbus_connection_send(dbus_conn, msg_error, NULL);
			dbus_connection_flush(dbus_conn);
	}
}

/* A server thread where all D-Bus messages are received and signals are sent. */
void *mpris_thread(void *unused ATTR_UNUSED)
{
	DBusMessage *reply;
// 	const char *path; /* The path an incoming message has been sent to. */
	const char *iface;

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
		// TODO: More signals needed
		LOCK(mpris_mutex);
// 		if (mpris_tracklist_changed) {
// 			mpris_tracklist_change_signal();
// 			mpris_tracklist_changed = 0;
// 		}
		if (mpris_track_changed) {
			mpris_track_change_signal();
			mpris_track_changed = 0;
		}
// 		if (mpris_caps_changed) {
// 			mpris_caps_change_signal();
// 			mpris_caps_changed = 0;
// 		}
		if (mpris_status_changed) {
			mpris_status_change_signal();
			mpris_status_changed = 0;
		}
		if (mpris_seeked) {
			mpris_seeked_signal();
			mpris_seeked = 0;
		}
		UNLOCK(mpris_mutex);

		/* Fetch an incoming message. */
		msg = dbus_connection_pop_message(dbus_conn);
		if (msg == NULL) continue;

		dbus_message_iter_init(msg, &args_in);

		/* Respond to all messages. They say in the specification some messages
		 * are sometimes no-ops, so this is the place to start moving code. */

		reply = dbus_message_new_method_return(msg);
		dbus_message_iter_init_append(reply, &args_out);

		/* Process the incoming message and prepare a response. */
		if ((iface = dbus_message_get_interface(msg)) != NULL) {
			if (!strcmp(MPRIS_IFACE_ROOT, iface))
				mpris_root_methods();
			else if (!strcmp(MPRIS_IFACE_PLAYER, iface))
				mpris_player_methods();
// 			else if (!strcmp(MPRIS_IFACE_TRACKLIST, iface))
// 				mpris_tracklist_methods();
			else if (!strcmp(PROPERTIES_IFACE, iface))
				mpris_properties();
			else if (!strcmp(INTROSPECTION_IFACE, iface) && dbus_message_is_method_call(msg, INTROSPECTION_IFACE, "Introspect")) {
				msg_add_string(&mpris_introspection); // TODO: check introspection data if it reflects what is really possible
			}
			else logit("MPRIS unknown interface: %s", iface);  // TODO: add Playlists and TrackList interfaces
		}

		/* Send the response and clean up. */
		dbus_connection_send(dbus_conn, reply, NULL);
		dbus_message_unref(msg);
		dbus_connection_flush(dbus_conn);
	}

	if (dbus_error_is_set(&dbus_err)) {
		logit("Error in D-Bus: %s", dbus_err.message);
		dbus_error_free(&dbus_err);
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

void mpris_position_change()
{
	LOCK(mpris_mutex);
	mpris_seeked = 1;
	UNLOCK(mpris_mutex);
}

void mpris_exit()
{
	pthread_mutex_destroy(&mpris_mutex);
}
