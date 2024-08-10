#include "boxedwine.h"
#include "x11.h"
#include "knativewindow.h"
#include "ksocket.h"

std::atomic_int XServer::nextId = 0x10000;
XServer* XServer::server;

XServer* XServer::getServer() {
	if (server) {
		return server;
	}
	BOXEDWINE_CRITICAL_SECTION;
	if (!server) {
		server = new XServer();
	}
	return server;
}

U32 XServer::getNextId() {
	return ++nextId;
}

XServer::XServer() {
	initAtoms();
}

void XServer::initAtoms() {
	// from https://github.com/aosm/X11/blob/master/xc/include/Xatom.h
	const char* names[] = {
		"NO_ATOM",
		"PRIMARY",
		"SECONDARY",
		"ARC",
		"ATOM",
		"BITMAP",
		"CARDINAL",
		"COLORMAP",
		"CURSOR",
		"CUT_BUFFER0",
		"CUT_BUFFER1",
		"CUT_BUFFER2",
		"CUT_BUFFER3",
		"CUT_BUFFER4",
		"CUT_BUFFER5",
		"CUT_BUFFER6",
		"CUT_BUFFER7",
		"DRAWABLE",
		"FONT",
		"INTEGER",
		"PIXMAP",
		"POINT",
		"RECTANGLE",
		"RESOURCE_MANAGER",
		"RGB_COLOR_MAP",
		"RGB_BEST_MAP",
		"RGB_BLUE_MAP",
		"RGB_DEFAULT_MAP",
		"RGB_GRAY_MAP",
		"RGB_GREEN_MAP",
		"RGB_RED_MAP",
		"STRING",
		"VISUALID",
		"WINDOW",
		"WM_COMMAND",
		"WM_HINTS",
		"WM_CLIENT_MACHINE",
		"WM_ICON_NAME",
		"WM_ICON_SIZE",
		"WM_NAME",
		"WM_NORMAL_HINTS",
		"WM_SIZE_HINTS",
		"WM_ZOOM_HINTS",
		"MIN_SPACE",
		"NORM_SPACE",
		"MAX_SPACE",
		"END_SPACE",
		"SUPERSCRIPT_X",
		"SUPERSCRIPT_Y",
		"SUBSCRIPT_X",
		"SUBSCRIPT_Y",
		"UNDERLINE_POSITION",
		"UNDERLINE_THICKNESS",
		"STRIKEOUT_ASCENT",
		"STRIKEOUT_DESCENT",
		"ITALIC_ANGLE",
		"X_HEIGHT",
		"QUAD_WIDTH",
		"WEIGHT",
		"POINT_SIZE",
		"RESOLUTION",
		"COPYRIGHT",
		"NOTICE",
		"FONT_NAME",
		"FAMILY_NAME",
		"FULL_NAME",
		"CAP_HEIGHT",
		"WM_CLASS",
		"WM_TRANSIENT_FOR",
		NULL
	};
	for (int i = 0; names[i] != NULL; i++) {
		setAtom(B(names[i]), i);
	}
	setAtom(B("_NET_WM_STATE"), _NET_WM_STATE);
	setAtom(B("_NET_WM_STATE_FULLSCREEN"), _NET_WM_STATE_FULLSCREEN);
	setAtom(B("_NET_WM_WINDOW_TYPE"), _NET_WM_WINDOW_TYPE);
	setAtom(B("_NET_WM_WINDOW_TYPE_NORMAL"), _NET_WM_WINDOW_TYPE_NORMAL);
	setAtom(B("_NET_WM_WINDOW_TYPE_DIALOG"), _NET_WM_WINDOW_TYPE_DIALOG);
	setAtom(B("WM_STATE"), WM_STATE);
	setAtom(B("_NET_WM_NAME"), _NET_WM_NAME);
	setAtom(B("_MOTIF_WM_HINTS"), _MOTIF_WM_HINTS);
	setAtom(B("_NET_WM_ICON"), _NET_WM_ICON);
	nextAtomID = 200;
}

void XServer::setAtom(const BString& name, U32 key) {
	atoms.set(key, name);
	reverseAtoms.set(name, key);
}

bool XServer::getAtom(U32 atom, BString& name) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(atomMutex);
	return atoms.get(atom, name);
}

U32 XServer::internAtom(const BString& name, bool onlyIfExists) {
	U32 result;

	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(atomMutex);
	if (reverseAtoms.get(name, result)) {
		return result;
	}
	if (onlyIfExists) {
		return None;
	}
	nextAtomID++;
	result = nextAtomID;
	setAtom(name, result);
	return result;
}

U32 XServer::getNextQuark() {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(quarkMutex);
	nextQuarkID++;
	return nextQuarkID;
}

XWindowPtr XServer::createNewWindow(U32 displayId, const XWindowPtr& parent, U32 width, U32 height, U32 depth, U32 x, U32 y, U32 c_class, U32 border_width) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(windowsMutex);
	XWindowPtr result = std::make_shared<XWindow>(displayId, parent, width, height, depth, x, y, c_class, border_width);
	windows.set(result->id, result);
	result->onCreate(result);
	return result;
}

XWindowPtr XServer::getRoot(KThread* thread) {
	if (!root) {
		KNativeWindowPtr nativeWindow = KNativeWindow::getNativeWindow();
		root = createNewWindow(0, nullptr, nativeWindow->screenWidth(), nativeWindow->screenHeight(), nativeWindow->screenBpp(), 0, 0, InputOutput, 0);

		U32 rect[] = { 0, 0, (U32)nativeWindow->screenWidth(), (U32)nativeWindow->screenHeight() };
		U32 atom = server->internAtom(B("_GTK_WORKAREAS_D0"), false);
		root->setProperty(thread, atom, XA_CARDINAL, 32, sizeof(U32) * 4, (U8*)&rect);
	}
	return root;
}

void XServer::draw(KThread* thread) {
	static U32 lastDraw;
	U32 now = KSystem::getMilliesSinceStart();
	if (now - lastDraw < 16) {
		return;
	}
	lastDraw = now;
	KNativeWindowPtr nativeWindow = KNativeWindow::getNativeWindow();
	nativeWindow->runOnUiThread([=]() {
		nativeWindow->clear();
		root->iterateChildren([](XWindowPtr child) {
			child->draw();
			});
		nativeWindow->present(thread);
		});
}

XWindowPtr XServer::getWindow(U32 window) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(windowsMutex);
	return windows.get(window);
}

int XServer::destroyWindow(U32 window) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(windowsMutex);
	XWindowPtr w = windows.get(window);
	if (!w) {
		return BadWindow;
	}
	windows.remove(window);
	return Success;
}

XPixmapPtr XServer::createNewPixmap(U32 width, U32 height, U32 depth) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pixmapsMutex);
	XPixmapPtr result = std::make_shared<XPixmap>(width, height, depth);
	pixmaps.set(result->id, result);
	return result;
}

XPixmapPtr XServer::getPixmap(U32 pixmap) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pixmapsMutex);
	return pixmaps.get(pixmap);
}

int XServer::removePixmap(U32 pixmap) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(pixmapsMutex);
	XPixmapPtr old = pixmaps.get(pixmap);
	if (old) {
		pixmaps.remove(pixmap);
		return Success;
	}
	return BadPixmap;
}

XDrawablePtr XServer::getDrawable(U32 xid) {
	XDrawablePtr result = getPixmap(xid);
	if (result) {
		return result;
	}
	return getWindow(xid);
}

void XServer::addCursor(const XCursorPtr& cursor) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(cursorsMutex);
	cursors.set(cursor->id, cursor);
}

XCursorPtr XServer::getCursor(U32 id) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(cursorsMutex);
	return cursors.get(id);
}

XGCPtr XServer::createGC(XDrawablePtr drawable) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(gcsMutex);
	XGCPtr result = std::make_shared<XGC>(drawable);
	gcs.set(result->id, result);
	return result;
}

XGCPtr XServer::getGC(U32 gc) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(gcsMutex);
	return gcs.get(gc);
}

void XServer::removeGC(U32 gc) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(gcsMutex);
	gcs.remove(gc);
}

void XServer::iterateEventMask(U32 wndId, U32 mask, std::function<void(const DisplayDataPtr& display)> callback) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(displayMutex);

	for (auto& display : displays) {
		if (display.value->getEventMask(wndId) & mask) {
			callback(display.value);
		}
	}
}

static U32 createScreen(KThread* thread, U32 displayAddress) {
	KMemory* memory = thread->memory;
	XServer* server = XServer::getServer();
	KNativeWindowPtr nativeWindow = KNativeWindow::getNativeWindow();
	U32 defaultVisual = Visual::create(thread, VisualIdBase, TrueColor, 0xFF0000, 0xFF00, 0xFF, 32, 256);
	U32 defaultDepth = Depth::create(thread, 32, defaultVisual, 1);

	U32 screenAddress = thread->process->alloc(thread, sizeof(Screen));
	X11_WRITED(Screen, screenAddress, display, displayAddress);
	X11_WRITED(Screen, screenAddress, width, nativeWindow->screenWidth());
	X11_WRITED(Screen, screenAddress, height, nativeWindow->screenHeight());
	X11_WRITED(Screen, screenAddress, mwidth, (U32)(nativeWindow->screenWidth() * 0.2646));
	X11_WRITED(Screen, screenAddress, mheight, (U32)(nativeWindow->screenHeight() * 0.2646));
	X11_WRITED(Screen, screenAddress, ndepths, 1); // 32 :TODO: do I need 8 and 16 or can I make Wine emulate it
	X11_WRITED(Screen, screenAddress, depths, defaultDepth);
	X11_WRITED(Screen, screenAddress, root_depth, 32);
	X11_WRITED(Screen, screenAddress, root_visual, defaultVisual);
	X11_WRITED(Screen, screenAddress, white_pixel, 0x00FFFFFF);
	X11_WRITED(Screen, screenAddress, black_pixel, 0);
	X11_WRITED(Screen, screenAddress, root, server->getRoot(thread)->id);
	// screen->cmap; // winex11 references this, but maybe not for TrueColor screen?
	
	return screenAddress;
}

U32 XServer::openDisplay(KThread* thread) {
	KMemory* memory = thread->memory;
	U32 displayAddress = thread->process->alloc(thread, sizeof(Display));

	U32 fdPairAddress = thread->process->alloc(thread, sizeof(U32) * 2);
	ksocketpair(thread, K_AF_UNIX, K_SOCK_STREAM, 0, fdPairAddress, 0);
	U32 fd1 = memory->readd(fdPairAddress);
	U32 fd2 = memory->readd(fdPairAddress + 4);
	thread->process->free(fdPairAddress);

	X11_WRITED(Display, displayAddress, fd, fd1);
	X11_WRITED(Display, displayAddress, proto_major_version, 11);
	X11_WRITED(Display, displayAddress, proto_minor_version, 4);

	U32 vendor = thread->process->createString(thread, B("Boxedwine.org"));
	X11_WRITED(Display, displayAddress, vendor, vendor);
	X11_WRITED(Display, displayAddress, byte_order, LSBFirst);
	X11_WRITED(Display, displayAddress, bitmap_bit_order, LSBFirst);
	X11_WRITED(Display, displayAddress, release, 1);  // NOT NEEDED /* Until version 1.10.4 rawinput was broken in XOrg, see https://bugs.freedesktop.org/show_bug.cgi?id=30068 */broken_rawevents = strstr(XServerVendor(gdi_display), "X.Org") && XVendorRelease(gdi_display) < 11004000;
	X11_WRITED(Display, displayAddress, request, 1);
	X11_WRITED(Display, displayAddress, display_name, vendor);
	X11_WRITED(Display, displayAddress, nscreens, 1);

	// values from Debian 11 32-bit
	S32 min_keycode = 0;
	S32 max_keycode = 0;
	XKeyboard::getMinMaxKeycodes(min_keycode, max_keycode);
	X11_WRITED(Display, displayAddress, min_keycode, min_keycode);
	X11_WRITED(Display, displayAddress, max_keycode, max_keycode);

	DisplayDataPtr data = std::make_shared<DisplayData>();
	data->displayAddress = displayAddress;	

	U32 screenAddress = createScreen(thread, displayAddress);
	X11_WRITED(Display, displayAddress, screens, screenAddress);
	U32 displayId = XServer::getNextId();
	X11_WRITED(Display, displayAddress, id, displayId);
	data->displayId = displayId;

	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(displayMutex);
	displays.set(displayId, data);

	return displayAddress;
}

DisplayDataPtr XServer::getDisplayDataByAddressOfDisplay(KMemory* memory, U32 address) {
	U32 id = X11_READD(Display, address, id);
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(displayMutex);
	return displays.get(id);
}

DisplayDataPtr XServer::getDisplayDataById(U32 id) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(displayMutex);
	return displays.get(id);
}

U32 XServer::getEventTime() {
	return KSystem::getMilliesSinceStart();
}