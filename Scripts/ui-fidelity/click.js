// osascript -l JavaScript click.js <screen x> <screen y>: left click at a screen position.
ObjC.import('CoreGraphics');
function run(argv) {
  const x = parseFloat(argv[0]), y = parseFloat(argv[1]);
  const p = {x: x, y: y};
  const mv = $.CGEventCreateMouseEvent(null, $.kCGEventMouseMoved, p, 0);
  $.CGEventPost($.kCGHIDEventTap, mv);
  delay(0.3);
  const right = argv[2] === 'r';
  const dn = $.CGEventCreateMouseEvent(null, right ? $.kCGEventRightMouseDown : $.kCGEventLeftMouseDown, p, right ? 1 : 0);
  $.CGEventPost($.kCGHIDEventTap, dn);
  delay(0.08);
  const up = $.CGEventCreateMouseEvent(null, right ? $.kCGEventRightMouseUp : $.kCGEventLeftMouseUp, p, right ? 1 : 0);
  $.CGEventPost($.kCGHIDEventTap, up);
}
