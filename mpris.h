#ifndef MPRIS_H
#define MPRIS_H

void mpris_init();
void *mpris_thread(void *unused ATTR_UNUSED);
void mpris_exit();
void mpris_track_change();
void mpris_status_change();
void mpris_caps_change();
void mpris_tracklist_change();
void mpris_position_change();


#endif
