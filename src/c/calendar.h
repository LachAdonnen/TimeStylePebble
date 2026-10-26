#pragma once
#include <pebble.h>

// upcoming calendar events, pushed from the phone (see src/pkjs/calendar.js)
// and kept in persistent storage so they survive disconnects/restarts.
// only used on emery (see meeting_bar.h); stubbed out everywhere else.

// persistent storage
#define CALENDAR_COUNT_PERSIST_KEY 300
#define CALENDAR_EVENT_PERSIST_KEY 301 // 301..(301 + CALENDAR_MAX_EVENTS - 1)

#define CALENDAR_MAX_EVENTS 3

typedef struct {
  int32_t start;    // epoch seconds
  int32_t end;      // epoch seconds
  char title[40];
  char location[32];
} CalendarEvent;

void Calendar_init();

// parses an incoming AppMessage. returns true if it was a calendar message
// (in which case the caller should not treat it as settings/weather)
bool Calendar_handleMessage(DictionaryIterator* iterator);

// returns the event to show at time `now`, or NULL if there is none.
// sets *inProgress if the event has already started.
const CalendarEvent* Calendar_getCurrentEvent(time_t now, bool* inProgress);
