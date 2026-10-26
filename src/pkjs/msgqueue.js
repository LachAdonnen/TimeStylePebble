/* serializes AppMessages to the watch, so weather, settings and calendar
   sends never overlap (the watch can only handle one message at a time) */

// if neither callback fires in this long, assume the send is lost and move on
var SEND_TIMEOUT_MS = 20000;

var queue = [];
var busy = false;

function sendNext() {
  if (busy || queue.length === 0) {
    return;
  }

  busy = true;
  var item = queue.shift();
  var finished = false;

  function finish(callback, e) {
    if (finished) {
      return;
    }
    finished = true;
    clearTimeout(timer);
    busy = false;

    if (callback) {
      callback(e);
    }

    sendNext();
  }

  var timer = setTimeout(function () {
    finish(item.onError, { error: 'timeout' });
  }, SEND_TIMEOUT_MS);

  Pebble.sendAppMessage(item.dict,
    function (e) { finish(item.onSuccess, e); },
    function (e) { finish(item.onError, e); }
  );
}

// same signature as Pebble.sendAppMessage
function send(dict, onSuccess, onError) {
  queue.push({ dict: dict, onSuccess: onSuccess, onError: onError });
  sendNext();
}

module.exports.send = send;
