#include <pebble.h>
#include "calendar.h"
#include "meeting_bar.h"

#if MEETING_BAR_ENABLED

// an event that's already started stays visible for this long
#define IN_PROGRESS_GRACE_SECONDS (10 * 60)

static CalendarEvent events[CALENDAR_MAX_EVENTS];
static int eventCount;

void Calendar_init() {
  eventCount = 0;

  if (persist_exists(CALENDAR_COUNT_PERSIST_KEY)) {
    int count = persist_read_int(CALENDAR_COUNT_PERSIST_KEY);

    for (int i = 0; i < count && i < CALENDAR_MAX_EVENTS; i++) {
      if (persist_read_data(CALENDAR_EVENT_PERSIST_KEY + i, &events[i], sizeof(CalendarEvent)) != sizeof(CalendarEvent)) {
        break;
      }

      // make sure strings are terminated even if storage is weird
      events[i].title[sizeof(events[i].title) - 1] = '\0';
      events[i].location[sizeof(events[i].location) - 1] = '\0';
      eventCount = i + 1;
    }
  }
}

static void copy_string(char* dest, size_t size, Tuple* t) {
  if (t != NULL && t->type == TUPLE_CSTRING) {
    strncpy(dest, t->value->cstring, size - 1);
    dest[size - 1] = '\0';
  } else {
    dest[0] = '\0';
  }
}

static int32_t read_int(Tuple* t) {
  if (t == NULL) {
    return 0;
  }

  // pkjs picks the smallest int type that fits, so handle all of them
  switch (t->length) {
    case 1:  return t->value->int8;
    case 2:  return t->value->int16;
    default: return t->value->int32;
  }
}

bool Calendar_handleMessage(DictionaryIterator* iterator) {
  Tuple* count_tuple = dict_find(iterator, MESSAGE_KEY_EvtCount);
  if (count_tuple == NULL) {
    return false;
  }

  // build the new list in a static scratch buffer so we can compare it
  static CalendarEvent newEvents[CALENDAR_MAX_EVENTS];
  memset(newEvents, 0, sizeof(newEvents));

  int newCount = read_int(count_tuple);
  if (newCount < 0) newCount = 0;
  if (newCount > CALENDAR_MAX_EVENTS) newCount = CALENDAR_MAX_EVENTS;

  for (int i = 0; i < newCount; i++) {
    newEvents[i].start = read_int(dict_find(iterator, MESSAGE_KEY_EvtStart + i));
    newEvents[i].end = read_int(dict_find(iterator, MESSAGE_KEY_EvtEnd + i));
    copy_string(newEvents[i].title, sizeof(newEvents[i].title),
                dict_find(iterator, MESSAGE_KEY_EvtTitle + i));
    copy_string(newEvents[i].location, sizeof(newEvents[i].location),
                dict_find(iterator, MESSAGE_KEY_EvtLoc + i));
  }

  // only touch persistent storage if something actually changed
  bool changed = (newCount != eventCount) ||
                 memcmp(newEvents, events, sizeof(CalendarEvent) * newCount) != 0;

  if (changed) {
    memcpy(events, newEvents, sizeof(events));
    eventCount = newCount;

    persist_write_int(CALENDAR_COUNT_PERSIST_KEY, eventCount);
    for (int i = 0; i < eventCount; i++) {
      persist_write_data(CALENDAR_EVENT_PERSIST_KEY + i, &events[i], sizeof(CalendarEvent));
    }
  }

  return true;
}

const CalendarEvent* Calendar_getCurrentEvent(time_t now, bool* inProgress) {
  // events are sorted by start time. an event that has started is only
  // shown for its first few minutes, otherwise show the next one to start
  for (int i = 0; i < eventCount; i++) {
    const CalendarEvent* e = &events[i];

    if (e->start > now) {
      *inProgress = false;
      return e;
    }

    int32_t graceEnd = e->start + IN_PROGRESS_GRACE_SECONDS;
    if (e->end < graceEnd) graceEnd = e->end;

    if (now < graceEnd) {
      *inProgress = true;
      return e;
    }
  }

  return NULL;
}

#else

void Calendar_init() {}

bool Calendar_handleMessage(DictionaryIterator* iterator) {
  // swallow calendar messages without touching the settings
  return dict_find(iterator, MESSAGE_KEY_EvtCount) != NULL;
}

const CalendarEvent* Calendar_getCurrentEvent(time_t now, bool* inProgress) {
  return NULL;
}

#endif
