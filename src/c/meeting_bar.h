#pragma once
#include <pebble.h>

// a permanent "next meeting" bar along the bottom of the screen, spanning the
// full width under both the clock and the sidebar. emery only.
#if defined(PBL_PLATFORM_EMERY)
#define MEETING_BAR_ENABLED 1
#define MEETING_BAR_HEIGHT 32
#else
#define MEETING_BAR_ENABLED 0
#define MEETING_BAR_HEIGHT 0
#endif

void MeetingBar_init(Window* window);
void MeetingBar_deinit();
void MeetingBar_redraw();
