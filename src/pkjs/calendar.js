/* upcoming calendar events for the "next meeting" bar (emery only).
   events come from the Core app's calendar data source and are sent to the
   watch in their own AppMessage; the watch decides what to show each minute */

var msgQueue = require('./msgqueue');
var keys = require('message_keys');

var MAX_EVENTS = 3;        // must match CALENDAR_MAX_EVENTS in calendar.h
var MAX_TITLE_BYTES = 39;  // CalendarEvent.title is 40 bytes incl. terminator
var MAX_LOC_BYTES = 31;    // CalendarEvent.location is 32 bytes incl. terminator
var MAX_FAILURES = 3;

// re-filter the last received list this often, so events that have ended
// make room for later ones even if the phone doesn't push a new list
var REFRESH_INTERVAL_MS = 10 * 60 * 1000;

// kept in memory rather than localStorage, so every launch sends at least once
// (the watch's copy may have been wiped by a reinstall)
var lastSentHash = null;

var lastInstances = [];
var currentFailures = 0;
var retryTimer = null;

// cut a string to at most maxBytes of UTF-8 without splitting a character
function truncateUtf8(str, maxBytes) {
  var bytes = 0;

  for (var i = 0; i < str.length; i++) {
    var code = str.charCodeAt(i);
    var size, units = 1;

    if (code < 0x80) {
      size = 1;
    } else if (code < 0x800) {
      size = 2;
    } else if (code >= 0xD800 && code <= 0xDBFF && i + 1 < str.length) {
      size = 4; // surrogate pair
      units = 2;
    } else {
      size = 3;
    }

    if (bytes + size > maxBytes) {
      return str.substring(0, i);
    }

    bytes += size;
    i += units - 1;
  }

  return str;
}

function textProp(props, name) {
  var p = props[name];
  return (p && p.shortText && p.shortText.text) ? String(p.shortText.text) : '';
}

function timestampProp(props, name) {
  var p = props[name];
  return (p && p.timestamp && p.timestamp.value !== undefined) ? Math.floor(Number(p.timestamp.value)) : 0;
}

// building abbreviations for conference room locations. buildings not listed
// fall back to their initials (multi-word) or first two letters (one word)
var BUILDING_ABBREVIATIONS = {
  'creatures': 'CR',
  'guilds': 'GU',
  'great hall': 'GH',
  'fortress': 'FO',
  'classroom': 'CL',
  'library': 'LI',
  'observatory': 'OB'
};

function abbreviateBuilding(building) {
  var known = BUILDING_ABBREVIATIONS[building.toLowerCase()];
  if (known) {
    return known;
  }

  var words = building.split(/\s+/);
  if (words.length > 1) {
    return words.map(function (w) { return w.charAt(0); }).join('').toUpperCase();
  }

  return building.substring(0, 2).toUpperCase();
}

// shortens conference room locations, e.g.
// "Conf Great Hall 2 Dragon Room (12 People) x4455" -> "Dragon Room (GH 2)"
// the floor is a number, G, or G followed by a number (G1, G2). anything not
// starting with "Conf" is left alone
var CONF_PREFIX = /^Conf\s+/i;
var CONF_FLOOR = /^(.+?)\s+(\d+|G\d*)\s+(.+)$/i;
var CONF_PEOPLE = /\s*\(\s*\d+\s+(?:People|Persons?)\s*\).*$/i;

function formatLocation(loc) {
  if (!CONF_PREFIX.test(loc)) {
    return loc;
  }

  // drop "Conf", then "(N People)" and the phone extension after it
  var rest = loc.replace(CONF_PREFIX, '').replace(CONF_PEOPLE, '').trim();

  var m = CONF_FLOOR.exec(rest);
  if (!m) {
    return rest;
  }

  return m[3] + ' (' + abbreviateBuilding(m[1]) + ' ' + m[2].toUpperCase() + ')';
}

function boolProp(props, name) {
  var p = props[name];
  return !!(p && p.boolean && p.boolean.value);
}

// turns the data source instances into the (at most 3) events the watch needs
function buildEventList(instances) {
  var now = Math.floor(Date.now() / 1000);
  var events = [];

  for (var i = 0; i < instances.length && events.length < MAX_EVENTS; i++) {
    var props = instances[i].properties || {};

    var start = timestampProp(props, 'starts_at');
    var end = timestampProp(props, 'ends_at');

    if (boolProp(props, 'all_day') || !start || end <= now) {
      continue;
    }

    events.push({
      start: start,
      end: end,
      title: truncateUtf8(textProp(props, 'name'), MAX_TITLE_BYTES),
      loc: truncateUtf8(formatLocation(textProp(props, 'location')), MAX_LOC_BYTES)
    });
  }

  // the source is already sorted, but don't rely on it
  events.sort(function (a, b) { return a.start - b.start; });

  return events;
}

// simple string hash (djb2), just to detect unchanged lists
function hashString(str) {
  var hash = 5381;
  for (var i = 0; i < str.length; i++) {
    hash = ((hash << 5) + hash + str.charCodeAt(i)) | 0;
  }
  return String(hash);
}

function sendLatest() {
  var events = buildEventList(lastInstances);
  var hash = hashString(JSON.stringify(events));

  if (hash === lastSentHash) {
    console.log('Calendar unchanged, not sending');
    return;
  }

  var dict = {};
  dict[keys.EvtCount] = events.length;

  for (var i = 0; i < events.length; i++) {
    dict[keys.EvtStart + i] = events[i].start;
    dict[keys.EvtEnd + i] = events[i].end;
    dict[keys.EvtTitle + i] = events[i].title;
    dict[keys.EvtLoc + i] = events[i].loc;
  }

  console.log('Sending ' + events.length + ' calendar events to Pebble');

  msgQueue.send(dict,
    function () {
      console.log('Calendar sent to Pebble successfully!');
      currentFailures = 0;
      lastSentHash = hash;
    },
    function () {
      // if we fail, wait a few seconds, then try again with the latest data
      if (currentFailures < MAX_FAILURES) {
        currentFailures++;
        clearTimeout(retryTimer);
        retryTimer = setTimeout(sendLatest, Math.floor(Math.random() * 7000) + 3000);
      } else {
        currentFailures = 0;
      }

      console.log('Error sending calendar to Pebble! Count: #' + currentFailures);
    }
  );
}

function onCalendarData(env) {
  lastInstances = (env && env.instances) ? env.instances : [];

  // log which calendars events come from, to help tell Outlook ones apart
  lastInstances.forEach(function (inst) {
    var props = inst.properties || {};
    console.log('Calendar event from "' + textProp(props, 'calendar') + '": ' + textProp(props, 'name'));
  });
  currentFailures = 0;
  sendLatest();
}

// fake data for the emulator, whose pypkjs has no subscribeToSource
function fakeTextProp(text) {
  return { shortText: { text: text } };
}

function fakeInstance(name, location, startMs, endMs, allDay) {
  var props = {
    name: fakeTextProp(name),
    starts_at: { timestamp: { value: Math.floor(startMs / 1000) } },
    ends_at: { timestamp: { value: Math.floor(endMs / 1000) } },
    all_day: { boolean: { value: allDay } }
  };

  if (location) {
    props.location = fakeTextProp(location);
  }

  return { properties: props };
}

function buildFakeEnvelope() {
  var MIN = 60 * 1000;
  var now = Date.now();
  var midnight = new Date(now);
  midnight.setHours(0, 0, 0, 0);
  var tomorrow = midnight.getTime() + 24 * 60 * MIN;

  return {
    instances: [
      fakeInstance('All Day Offsite', '', midnight.getTime(), tomorrow, true),
      fakeInstance('Standup', 'Teams', now - 5 * MIN, now + 10 * MIN, false),
      fakeInstance('Quarterly Planning Review', 'Conf Great Hall 2 Dragon Room (12 People) x4455', now + 40 * MIN, now + 100 * MIN, false),
      fakeInstance('1:1 with Sam', '', tomorrow + 9 * 60 * MIN, tomorrow + 9.5 * 60 * MIN, false)
    ]
  };
}

function isEmery() {
  try {
    return Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo().platform === 'emery';
  } catch (err) {
    return false;
  }
}

function start() {
  // the meeting bar only exists on emery
  if (!isEmery()) {
    return;
  }

  if (typeof Pebble.subscribeToSource === 'function') {
    Pebble.subscribeToSource({
      category: 'calendar',
      item: 'event',
      onData: onCalendarData,
      onError: function (e) {
        // errors don't end the subscription; PERMISSION_DENIED may just mean
        // the user hasn't approved calendar access yet
        console.log('Calendar data source error: ' + JSON.stringify(e));
      }
    });
  } else {
    console.log('Calendar data source unavailable, using fake events');
    onCalendarData(buildFakeEnvelope());
  }

  setInterval(sendLatest, REFRESH_INTERVAL_MS);
}

module.exports.start = start;
