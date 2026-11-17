ObjC.import('CoreGraphics');
ObjC.import('Foundation');
const list = ObjC.deepUnwrap(ObjC.castRefToObject($.CGWindowListCopyWindowInfo(1<<0|1<<4, 0)));
const out = list.filter(w => w.kCGWindowOwnerName === 'Obelisk' && w.kCGWindowLayer === 0).map(w => ({id:w.kCGWindowNumber, x:w.kCGWindowBounds.X, y:w.kCGWindowBounds.Y, w:w.kCGWindowBounds.Width, h:w.kCGWindowBounds.Height, name:w.kCGWindowName}));
JSON.stringify(out);
