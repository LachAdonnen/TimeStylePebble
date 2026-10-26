#include <pebble.h>
#include "meeting_bar.h"
#include "calendar.h"
#include "settings.h"

#if MEETING_BAR_ENABLED

// width of the left column holding the start time and relative time
#define TIME_COLUMN_WIDTH 40

static Layer* meetingBarLayer;

static void format_clock_time(char* buf, size_t size, time_t t) {
  struct tm* tm = localtime(&t);
  int hour = tm->tm_hour;

  if (!clock_is_24h_style()) {
    hour = hour % 12;
    if (hour == 0) hour = 12;
  }

  snprintf(buf, size, "%d:%02d", hour, tm->tm_min);
}

// "now", "in 3m", "in 2h", or the weekday if it isn't today
static void format_relative_time(char* buf, size_t size, const CalendarEvent* e,
                                 bool inProgress, time_t now) {
  struct tm* tm = localtime(&now);
  int nowYear = tm->tm_year;
  int nowYday = tm->tm_yday;

  time_t start = e->start;
  tm = localtime(&start);

  if (tm->tm_year != nowYear || tm->tm_yday != nowYday) {
    strftime(buf, size, "%a", tm);
  } else if (inProgress) {
    snprintf(buf, size, "now");
  } else {
    // round up, so a meeting 30 seconds away still says "in 1m"
    int minutes = (e->start - now + 59) / 60;

    if (minutes < 60) {
      snprintf(buf, size, "in %dm", minutes);
    } else {
      snprintf(buf, size, "in %dh", minutes / 60);
    }
  }
}

static void draw_text(GContext* ctx, const char* text, const char* fontKey,
                      GRect rect, GTextAlignment align) {
  graphics_draw_text(ctx, text, fonts_get_system_font(fontKey), rect,
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void update_meeting_bar(Layer* l, GContext* ctx) {
  GRect bounds = layer_get_bounds(l);

  graphics_context_set_fill_color(ctx, settings.sidebarColor);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, settings.sidebarTextColor);

  time_t now = time(NULL);
  bool inProgress = false;
  const CalendarEvent* e = Calendar_getCurrentEvent(now, &inProgress);

  if (e == NULL) {
    draw_text(ctx, "No more meetings", FONT_KEY_GOTHIC_14,
              GRect(0, (bounds.size.h - 18) / 2, bounds.size.w, 18), GTextAlignmentCenter);
    return;
  }

  char timeText[8];
  char relativeText[16];
  format_clock_time(timeText, sizeof(timeText), e->start);
  format_relative_time(relativeText, sizeof(relativeText), e, inProgress, now);

  // left column: start time above relative time
  draw_text(ctx, timeText, FONT_KEY_GOTHIC_18_BOLD,
            GRect(0, -3, TIME_COLUMN_WIDTH, 22), GTextAlignmentCenter);
  draw_text(ctx, relativeText, FONT_KEY_GOTHIC_14,
            GRect(0, 14, TIME_COLUMN_WIDTH, 18), GTextAlignmentCenter);

  // divider
  graphics_context_set_stroke_color(ctx, settings.sidebarTextColor);
  graphics_draw_line(ctx, GPoint(TIME_COLUMN_WIDTH, 4),
                     GPoint(TIME_COLUMN_WIDTH, bounds.size.h - 5));

  // right column: title above location (or end time if there's no location)
  int x = TIME_COLUMN_WIDTH + 4;
  int w = bounds.size.w - x - 2;
  draw_text(ctx, e->title, FONT_KEY_GOTHIC_14_BOLD, GRect(x, -1, w, 18), GTextAlignmentLeft);

  if (e->location[0] != '\0') {
    draw_text(ctx, e->location, FONT_KEY_GOTHIC_14, GRect(x, 14, w, 18), GTextAlignmentLeft);
  } else if (e->end > e->start) {
    char endText[16];
    char endTime[8];
    format_clock_time(endTime, sizeof(endTime), e->end);
    snprintf(endText, sizeof(endText), "until %s", endTime);
    draw_text(ctx, endText, FONT_KEY_GOTHIC_14, GRect(x, 14, w, 18), GTextAlignmentLeft);
  }
}

void MeetingBar_init(Window* window) {
  Layer* root = window_get_root_layer(window);
  GRect screen = layer_get_bounds(root);

  meetingBarLayer = layer_create(GRect(0, screen.size.h - MEETING_BAR_HEIGHT,
                                       screen.size.w, MEETING_BAR_HEIGHT));
  layer_set_update_proc(meetingBarLayer, update_meeting_bar);
  layer_add_child(root, meetingBarLayer);
}

void MeetingBar_deinit() {
  layer_destroy(meetingBarLayer);
}

void MeetingBar_redraw() {
  layer_mark_dirty(meetingBarLayer);
}

#else

void MeetingBar_init(Window* window) {}
void MeetingBar_deinit() {}
void MeetingBar_redraw() {}

#endif
