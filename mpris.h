#ifndef MPRIS_H
#define MPRIS_H

#define MPRIS_TIMEOUT			50
#define MPRIS_BUS_NAME			"org.mpris.moc"
#define MPRIS_IFACE				"org.freedesktop.MediaPlayer"
#define INTROSPECTION_IFACE		"org.freedesktop.DBus.Introspectable"

#define CAN_GO_NEXT				1 << 0
#define CAN_GO_PREV				1 << 1
#define CAN_PAUSE				1 << 2
#define CAN_PLAY				1 << 3
#define CAN_SEEK				1 << 4
#define CAN_PROVIDE_METADATA	1 << 5
#define CAN_HAS_TRACKLIST		1 << 6

void mpris_init();
void *mpris_thread(void *unused ATTR_UNUSED);
void mpris_exit();
void mpris_track_change();
void mpris_status_change();
void mpris_caps_change();
void mpris_tracklist_change();

#endif
