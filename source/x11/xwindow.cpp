#include "boxedwine.h"
#include "x11.h"
#include "knativewindow.h"

void XWindowChanges::read(KMemory* memory, U32 address) {
	x = memory->readd(address);
	y = memory->readd(address + 4);
	width = memory->readd(address + 8);
	height = memory->readd(address + 12);
	border_width = memory->readd(address + 16);
	sibling = memory->readd(address + 20);
	stack_mode = memory->readd(address + 24);
};

void XKeyEvent::read(KMemory* memory, U32 address) {
	type = (S32)memory->readd(address); address += 4;
	serial = memory->readd(address); address += 4;
	send_event = (S32)memory->readd(address); address += 4;
	display = memory->readd(address); address += 4;
	window = memory->readd(address); address += 4;
	root = memory->readd(address); address += 4;
	subwindow = memory->readd(address); address += 4;
	time = memory->readd(address); address += 4;
	x = (S32)memory->readd(address); address += 4; 
	y = (S32)memory->readd(address); address += 4;
	x_root = (S32)memory->readd(address); address += 4;
	y_root = (S32)memory->readd(address); address += 4;
	state = memory->readd(address); address += 4;
	keycode = memory->readd(address); address += 4;
	same_screen = (S32)memory->readd(address);
}

void XSetWindowAttributes::read(KMemory* memory, U32 address) {
	background_pixmap = memory->readd(address); address += 4;
	background_pixel = memory->readd(address); address += 4;
	border_pixmap = memory->readd(address); address += 4;
	border_pixel = memory->readd(address); address += 4;
	bit_gravity = (S32)memory->readd(address); address += 4;
	win_gravity = (S32)memory->readd(address); address += 4;
	backing_store = (S32)memory->readd(address); address += 4;
	backing_planes = memory->readd(address); address += 4;
	backing_pixel = memory->readd(address); address += 4;
	save_under = (S32)memory->readd(address); address += 4;
	event_mask = (S32)memory->readd(address); address += 4;
	do_not_propagate_mask = (S32)memory->readd(address); address += 4;
	override_redirect = (S32)memory->readd(address); address += 4;
	colormap = memory->readd(address); address += 4;
	cursor = memory->readd(address);
}

XSetWindowAttributes* XSetWindowAttributes::get(KMemory* memory, U32 address, XSetWindowAttributes* tmp) {
	if (!address) {
		return nullptr;
	}
#ifndef UNALIGNED_MEMORY
	if ((address & K_PAGE_MASK) + sizeof(XSetWindowAttributes) < K_PAGE_SIZE) {
		return (XSetWindowAttributes*)memory->getIntPtr(address, true);
	}
#endif
	tmp->read(memory, address);
	return tmp;
}

#define CWBackPixmap		(1L<<0)
#define CWBackPixel		(1L<<1)
#define CWBorderPixmap		(1L<<2)
#define CWBorderPixel           (1L<<3)
#define CWBitGravity		(1L<<4)
#define CWWinGravity		(1L<<5)
#define CWBackingStore          (1L<<6)
#define CWBackingPlanes	        (1L<<7)
#define CWBackingPixel	        (1L<<8)
#define CWOverrideRedirect	(1L<<9)
#define CWSaveUnder		(1L<<10)
#define CWEventMask		(1L<<11)
#define CWDontPropagate	        (1L<<12)
#define CWColormap		(1L<<13)
#define CWCursor	        (1L<<14)

void XSetWindowAttributes::copyWithMask(XSetWindowAttributes* attributes, U32 valueMask) {
	if (valueMask & CWBackPixmap) {
		background_pixmap = attributes->background_pixmap;
	}
	if (valueMask & CWBackPixel) {
		background_pixel = attributes->background_pixel;
	}
	if (valueMask & CWBorderPixmap) {
		border_pixmap = attributes->border_pixmap;
	}
	if (valueMask & CWBorderPixel) {
		border_pixel = attributes->border_pixel;
	}
	if (valueMask & CWBitGravity) {
		bit_gravity = attributes->bit_gravity;
	}
	if (valueMask & CWWinGravity) {
		win_gravity = attributes->win_gravity;
	}
	if (valueMask & CWBackingStore) {
		backing_store = attributes->backing_store;
	}
	if (valueMask & CWBackingPlanes) {
		backing_planes = attributes->backing_planes;
	}
	if (valueMask & CWBackingPixel) {
		backing_pixel = attributes->backing_pixel;
	}
	if (valueMask & CWSaveUnder) {
		save_under = attributes->save_under;
	}
	if (valueMask & CWEventMask) {
		event_mask = attributes->event_mask;
	}
	if (valueMask & CWDontPropagate) {
		do_not_propagate_mask = attributes->do_not_propagate_mask;
	}
	if (valueMask & CWOverrideRedirect) {
		override_redirect = attributes->override_redirect;
	}
	if (valueMask & CWColormap) {
		colormap = attributes->colormap;
	}
	if (valueMask & CWCursor) {
		cursor = attributes->cursor;
	}
}

XWindow::XWindow(U32 displayId, const XWindowPtr& parent, U32 width, U32 height, U32 depth, U32 x, U32 y, U32 c_class, U32 border_width) : XDrawable(width, height, depth), parent(parent), x(x), y(y), displayId(displayId), c_class(c_class), border_width(border_width) {
	if (parent) {
		attributes.border_pixmap = parent->attributes.border_pixmap;
		attributes.colormap = parent->attributes.colormap;					
	}
}

void XWindow::onDestroy() {
	if (parent) {
		{
			BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(parent->childrenMutex);
			parent->children.remove(id);
			int index = vectorIndexOf(parent->zchildren, shared_from_this());
			if (index >= 0 && index < parent->zchildren.size()) {
				parent->zchildren.erase(parent->zchildren.begin() + index);
			}
		}
		XServer::getServer()->iterateEventMask(parent->id, SubstructureNotifyMask, [=](const DisplayDataPtr& data) {
			XEvent event = {};
			event.type = DestroyNotify;
			event.xdestroywindow.event = parent->id;
			event.xdestroywindow.window = id;
			event.xcreatewindow.serial = data->getNextEventSerial();
			event.xcreatewindow.display = data->displayAddress;
			data->putEvent(event);

			if (XServer::getServer()->trace) {
				BString log;
				log.append(data->displayId, 16);
				log += " Event";
				log += " DestroyNotify";
				log += " event=";
				log.append(parent->id, 16);
				log += " window=";
				log.append(id, 16);				
				klog(log.c_str());
			}
			});
		// unlike CreateNotify, this can generate StructureNotify
		XServer::getServer()->iterateEventMask(id, StructureNotifyMask, [=](const DisplayDataPtr& data) {
			XEvent event = {};
			event.type = DestroyNotify;
			event.xdestroywindow.event = id;
			event.xdestroywindow.window = id;
			event.xcreatewindow.serial = data->getNextEventSerial();
			event.xcreatewindow.display = data->displayAddress;
			data->putEvent(event);

			if (XServer::getServer()->trace) {
				BString log;
				log.append(data->displayId, 16);
				log += " Event";
				log += " DestroyNotify";
				log += " event=";
				log.append(id, 16);
				log += " window=";
				log.append(id, 16);				
				klog(log.c_str());
			}
			});
	}
}

void XWindow::onCreate() {	
	if (parent) {
		{
			BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(parent->childrenMutex);
			parent->children.set(id, shared_from_this());
			parent->zchildren.push_back(shared_from_this());
		}
		XServer::getServer()->iterateEventMask(parent->id, SubstructureNotifyMask, [=](const DisplayDataPtr& data) {
			XEvent event = {};
			event.type = CreateNotify;
			event.xcreatewindow.parent = parent->id;
			event.xcreatewindow.window = id;
			event.xcreatewindow.x = x;
			event.xcreatewindow.y = y;
			event.xcreatewindow.width = width();
			event.xcreatewindow.height = height();
			event.xcreatewindow.border_width = border_width;
			event.xcreatewindow.serial = data->getNextEventSerial();
			event.xcreatewindow.display = data->displayAddress;
			data->putEvent(event);

			if (XServer::getServer()->trace) {
				BString log;
				log.append(data->displayId, 16);
				log += " Event";
				log += " CreateNotify";
				log += " window=";
				log.append(id, 16);
				log += " parent=";
				log.append(parent->id, 16);
				klog(log.c_str());
			}
			});
		// doesn't generate StructureNotifyMask
	}
}

void XWindow::setAttributes(const DisplayDataPtr& data, XSetWindowAttributes* attributes, U32 valueMask) {
	this->attributes.copyWithMask(attributes, valueMask);
	if (valueMask & CWEventMask) {
		data->setEventMask(id, attributes->event_mask);
	}
}

void XWindow::iterateMappedChildrenFrontToBack(std::function<bool(const XWindowPtr& child)> callback) {
	for (int i = (int)zchildren.size() - 1; i >= 0; i--) {
		const XWindowPtr& child = zchildren.at(i);
		if (child->mapped()) {
			if (!callback(child)) {
				break;
			}
		}
	}
}

void XWindow::iterateMappedChildrenBackToFront(std::function<bool(const XWindowPtr& child)> callback) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(childrenMutex);
	for (auto& child : zchildren) {
		if (child->mapped()) {
			if (!callback(child)) {
				break;
			}
		}
	}
}

void XWindow::windowToScreen(S32& x, S32& y) {
	XWindowPtr p = shared_from_this();

	while (p) {
		x += p->x;
		y += p->y;
		p = p->parent;
	}
}

void XWindow::screenToWindow(S32& x, S32& y) {
	XWindowPtr p = shared_from_this();

	while (p) {
		x -= p->x;
		y -= p->y;
		p = p->parent;
	}
}

void XWindow::setTextProperty(const DisplayDataPtr& data, KThread* thread, XTextProperty* name, Atom property, bool trace) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(propertiesMutex);
	properties.setProperty(property, name->encoding, name->format, name->byteLen(thread->memory), name->value);
	if (trace && XServer::getServer()->trace) {
		XPropertyPtr prop = properties.getProperty(property);
		if (prop) {
			BString log;

			log.append(data->displayId, 16);
			log += " ChangeProperty mode=Replace(0x00) window=";
			log.append(id, 16);
			log += " ";
			log += prop->log();
			klog(log.c_str());
		}
	}
}

XPropertyPtr XWindow::getProperty(U32 atom) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(propertiesMutex);
	return properties.getProperty(atom);
}

void XWindow::setProperty(const DisplayDataPtr& data, U32 atom, U32 type, U32 format, U32 length, U8* value, bool trace) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(propertiesMutex);
	properties.setProperty(atom, type, format, length, value);	

	if (trace && XServer::getServer()->trace) {
		XPropertyPtr prop = properties.getProperty(atom);
		if (prop) {
			BString log;

			log.append(data->displayId, 16);
			log += " ChangeProperty mode=Replace(0x00) window=";
			log.append(id, 16);
			log += " ";
			log += prop->log();
			klog(log.c_str());
		}
	}

	XServer::getServer()->iterateEventMask(id, PropertyChangeMask, [=](const DisplayDataPtr& data) {
		XEvent event = {};
		event.xproperty.type = PropertyNotify;
		event.xproperty.atom = atom;
		event.xproperty.display = data->displayAddress;
		event.xproperty.send_event = False;
		event.xproperty.state = PropertyNewValue;
		event.xproperty.window = id;
		event.xproperty.serial = data->getNextEventSerial();
		event.xproperty.time = XServer::getServer()->getEventTime();
		data->putEvent(event);

		if (XServer::getServer()->trace) {
			BString log;
			log.append(data->displayId, 16);
			log += " Event";
			log += " PropertyNotify";
			log += " window=";
			log.append(id, 16);
			log += " atom=";
			log.append(atom, 16);
			log += "(";
			BString name;
			XServer::getServer()->getAtom(atom, name);
			log += name;
			log += ")";
			log += " state=NewValue";
			klog(log.c_str());
		}
		});
}

void XWindow::setProperty(const DisplayDataPtr& data, U32 atom, U32 type, U32 format, U32 length, U32 value, bool trace) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(propertiesMutex);
	properties.setProperty(atom, type, format, length, value);
	if (trace && XServer::getServer()->trace) {
		XPropertyPtr prop = getProperty(atom);
		if (prop) {
			BString log;

			log.append(data->displayId, 16);
			log += " ChangeProperty mode=Replace(0x00) window=";
			log.append(id, 16);
			log += " ";
			log += prop->log();
			klog(log.c_str());
		}
	}

	XServer::getServer()->iterateEventMask(id, PropertyChangeMask, [=](const DisplayDataPtr& data) {
		XEvent event = {};
		event.xproperty.type = PropertyNotify;
		event.xproperty.atom = atom;
		event.xproperty.display = data->displayAddress;
		event.xproperty.send_event = False;
		event.xproperty.state = PropertyNewValue;
		event.xproperty.window = id;
		event.xproperty.serial = data->getNextEventSerial();
		event.xproperty.time = XServer::getServer()->getEventTime();
		data->putEvent(event);

		if (XServer::getServer()->trace) {
			BString log;
			log.append(data->displayId, 16);
			log += " Event";
			log += " PropertyNotify";
			log += " window=";
			log.append(id, 16);
			log += " atom=";
			log.append(atom, 16);
			log += "(";
			BString name;
			XServer::getServer()->getAtom(atom, name);
			log += name;
			log += ")";
			log += " state=NewValue";
			klog(log.c_str());
		}
		});
}

void XWindow::deleteProperty(const DisplayDataPtr& data, U32 atom, bool trace) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(propertiesMutex);
	XPropertyPtr prop = properties.getProperty(atom);
	properties.deleteProperty(atom);
	if (prop) {
		XServer::getServer()->iterateEventMask(id, PropertyChangeMask, [=](const DisplayDataPtr& data) {
			XEvent event = {};
			event.xproperty.type = PropertyNotify;
			event.xproperty.atom = atom;
			event.xproperty.display = data->displayAddress;
			event.xproperty.send_event = False;
			event.xproperty.state = PropertyDelete;
			event.xproperty.window = id;
			event.xproperty.serial = data->getNextEventSerial();
			event.xproperty.time = XServer::getServer()->getEventTime();
			data->putEvent(event);

			if (XServer::getServer()->trace) {
				BString log;
				log.append(data->displayId, 16);
				log += " Event";
				log += " PropertyNotify";
				log += " window=";
				log.append(id, 16);
				log += " atom=";
				log.append(atom, 16);
				log += "(";
				BString name;
				XServer::getServer()->getAtom(atom, name);
				log += name;
				log += ")";
				log += " state=Delete";
				klog(log.c_str());
			}
			});
	}
}

int XWindow::handleNetWmStatePropertyEvent(const DisplayDataPtr& data, const XEvent& event) {
	U32 count = 0;
	for (U32 i = 1; i < 5; i++) {
		if (event.xclient.data.l[i] <= 1) {
			break;
		}
		count++;
	}
	XPropertyPtr prop = getProperty(event.xclient.message_type);
	if (!prop) {
		if (event.xclient.data.l[0] == 1) {
			setProperty(data, (U32)event.xclient.message_type, XA_ATOM, 32, count * 4, (U8*)&event.xclient.data.l[1]);
		}
	} else {
		U32* values = (U32*)prop->value;
		U32 existingCount = prop->length / 4;
		U32 removedCount = 0;
		if (event.xclient.data.l[0] == 0) {
			for (U32 i = 0; i < count; i++) {
				for (U32 j = 0; j < existingCount; j++) {
					if (event.xclient.data.l[i + 1] == values[j]) {
						values[j] = 0;
						removedCount++;
					}
				}
			}
			if (removedCount) {
				U32 newCount = existingCount - removedCount;
				if (!newCount) {
					deleteProperty(data, event.xclient.message_type);
				} else {
					U32* newValues = new U32[newCount];
					U32 index = 0;

					for (U32 i = 0; i < existingCount; i++) {
						if (values[i]) {
							newValues[index] = values[i];
							index++;
						}
					}
					setProperty(data, (U32)event.xclient.message_type, XA_ATOM, 32, newCount * 4, (U8*)newValues);
					delete[] newValues;
				}
			}
		} else if (event.xclient.data.l[0] == 1) {
			U32* newValues = new U32[existingCount + count];
			U32 addIndex = existingCount;

			memcpy(newValues, values, sizeof(U32) * existingCount);
			for (U32 i = 0; i < count; i++) {
				bool found = false;
				for (U32 j = 0; j < existingCount; j++) {
					if (event.xclient.data.l[i + 1] == values[j]) {
						found = true;
						break;
					}
				}				
				if (!found) {
					newValues[addIndex] = event.xclient.data.l[i + 1];
					addIndex++;
				}
			}
			if (addIndex != existingCount) {
				setProperty(data, (U32)event.xclient.message_type, XA_ATOM, 32, addIndex * 4, (U8*)newValues);
			}
			delete[] newValues;
		} else if (event.xclient.data.l[0] == 1) {
			kpanic("XWindow::handleNetWmStatePropertyEvent toggle not handled");
		} else {
			return BadValue;
		}
	}
	return Success;
}

void XWindow::exposeNofity(const DisplayDataPtr& data, S32 x, S32 y, S32 width, S32 height, S32 count) {
	XEvent event = {};
	event.xexpose.type = Expose;
	event.xexpose.display = data->displayAddress;
	event.xexpose.send_event = False;
	event.xexpose.window = id;
	event.xexpose.serial = data->getNextEventSerial();
	event.xexpose.x = x;
	event.xexpose.y = y;
	event.xexpose.width = width;
	event.xexpose.height = height;
	event.xexpose.count = count;
	data->putEvent(event);

	if (XServer::getServer()->trace) {
		BString log;
		log.append(data->displayId, 16);
		log += " Event";
		log += " Expose";
		log += " win=";
		log.append(id, 16);
		log += " x=";
		log += x;
		log += " y=";
		log += y;
		log += " width=";
		log += width;
		log += " height=";
		log += height;
	}
	iterateMappedChildrenBackToFront([](const XWindowPtr& child) {
		if (child->c_class == InputOutput) {
			XServer::getServer()->iterateEventMask(child->id, ExposureMask, [=](const DisplayDataPtr& data) {
				child->exposeNofity(data, 0, 0, child->width(), child->height(), 0);
				});
		}
		return true;
		});
}

void XWindow::setWmState(const DisplayDataPtr& data, U32 state, U32 icon) {
	XServer* server = XServer::getServer();
	XWMState wmState;

	wmState.state = state;
	wmState.icon = 0;
	setProperty(data, WM_STATE, WM_STATE, 32, 8, (U8*)&wmState);
}

int XWindow::mapWindow(const DisplayDataPtr& data) {
	if (isMapped) {
		return Success;
	}
	isMapped = true;
	setDirty();
	setWmState(data, NormalState, 0);
	if (parent) {
		XServer::getServer()->iterateEventMask(parent->id, SubstructureNotifyMask, [=](const DisplayDataPtr& data) {
			XEvent event = {};
			event.xmap.type = MapNotify;
			event.xmap.display = data->displayAddress;
			event.xmap.send_event = False;
			event.xmap.event = parent->id;
			event.xmap.window = id;
			event.xmap.serial = data->getNextEventSerial();
			event.xmap.override_redirect = this->attributes.override_redirect;
			data->putEvent(event);

			if (XServer::getServer()->trace) {
				BString log;
				log.append(data->displayId, 16);
				log += " Event";
				log += " MapNotify";
				log += " win=";
				log.append(id, 16);
				log += " event=";
				log.append(parent->id, 16);
				klog(log.c_str());
			}
			});
	}
	XServer::getServer()->iterateEventMask(id, StructureNotifyMask, [=](const DisplayDataPtr& data) {
		XEvent event = {};
		event.xmap.type = MapNotify;
		event.xmap.display = data->displayAddress;
		event.xmap.send_event = False;
		event.xmap.event = id;
		event.xmap.window = id;
		event.xmap.serial = data->getNextEventSerial();
		event.xmap.override_redirect = this->attributes.override_redirect;
		data->putEvent(event);

		if (XServer::getServer()->trace) {
			BString log;
			log.append(data->displayId, 16);
			log += " Event";
			log += " MapNotify";
			log += " win=";
			log.append(id, 16);
			log += " event=";
			log.append(id, 16);
		}
		});
	if (attributes.backing_store != NotUseful) {
		kwarn("XWindow::mapWindow backing_store was expected to be NotUseful");
	}
	if (c_class == InputOutput) {
		XServer::getServer()->iterateEventMask(id, ExposureMask, [=](const DisplayDataPtr& data) {
			exposeNofity(data, 0, 0, width(), height(), 0);
			});		
	}
	return Success;
}

int XWindow::unmapWindow(const DisplayDataPtr& data) {
	if (!isMapped) {
		return Success;
	}
	isMapped = false;	
	setWmState(data, WithdrawnState, 0);
	if (parent) {
		setDirty();
		XServer::getServer()->iterateEventMask(parent->id, SubstructureNotifyMask, [=](const DisplayDataPtr& data) {
			XEvent event = {};
			event.xmap.type = UnmapNotify;
			event.xmap.display = data->displayAddress;
			event.xmap.send_event = False;
			event.xmap.event = parent->id;
			event.xmap.window = id;
			event.xmap.serial = data->getNextEventSerial();
			event.xmap.override_redirect = this->attributes.override_redirect;
			data->putEvent(event);

			if (XServer::getServer()->trace) {
				BString log;
				log.append(data->displayId, 16);
				log += " Event";
				log += " UnmapNotify";
				log += " win=";
				log.append(id, 16);
				log += " event=";
				log.append(parent->id, 16);
			}
			});
	}
	XServer::getServer()->iterateEventMask(id, StructureNotifyMask, [=](const DisplayDataPtr& data) {
		XEvent event = {};
		event.xmap.type = UnmapNotify;
		event.xmap.display = data->displayAddress;
		event.xmap.send_event = False;
		event.xmap.event = id;
		event.xmap.window = id;
		event.xmap.serial = data->getNextEventSerial();
		event.xmap.override_redirect = this->attributes.override_redirect;
		data->putEvent(event);

		if (XServer::getServer()->trace) {
			BString log;
			log.append(data->displayId, 16);
			log += " Event";
			log += " UnmapNotify";
			log += " win=";
			log.append(id, 16);
			log += " event=";
			log.append(id, 16);
		}
		});
	if (c_class == InputOutput && parent) {
		XServer::getServer()->iterateEventMask(parent->id, ExposureMask, [=](const DisplayDataPtr& data) {
			exposeNofity(data, 0, 0, parent->width(), parent->height(), 0);
			});
	}
	return Success;
}

void XWindow::setDirty() {
	if (!isDirty) {
		isDirty = true;
		XServer::getServer()->isDisplayDirty = true;
	}
}

void XWindow::draw() {
	if (c_class == InputOnly || !isMapped) {
		return;
	}

	KNativeWindowPtr nativeWindow = KNativeWindow::getNativeWindow();
	WndPtr wnd = nativeWindow->getWnd(id);
	if (wnd) {
		nativeWindow->putBitsOnWnd(wnd, data, bytes_per_line, x, y, width(), height(), isDirty);
		isDirty = false;
	}
	iterateMappedChildrenBackToFront([](const XWindowPtr& child) {
		if (child->c_class == InputOutput) {
			child->draw();
		}
		return true;
		});
}

XWindowPtr XWindow::previousSibling() {
	if (parent) {
		BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(parent->childrenMutex);
		for (U32 i = 0; i < (U32)parent->zchildren.size(); i++) {
			if (parent->zchildren[i]->id == id) {
				if (i > 0) {
					return parent->zchildren[i - 1];
				}
				return nullptr;
			}
		}
	}
	return nullptr;
}

void XWindow::configureNotify() {
	XWindowPtr prev = previousSibling();
	Drawable above = None;
	if (prev) {
		above = prev->id;
	}

	if (parent) {
		XServer::getServer()->iterateEventMask(parent->id, StructureNotifyMask, [=](const DisplayDataPtr& data) {
			XEvent event = {};
			event.type = ConfigureNotify;
			event.xconfigure.event = parent->id;
			event.xconfigure.window = id;
			event.xconfigure.x = x;
			event.xconfigure.y = y;
			event.xconfigure.width = width();
			event.xconfigure.height = height();
			event.xconfigure.border_width = border_width;
			event.xconfigure.above = above;
			event.xconfigure.override_redirect = attributes.override_redirect;
			event.xconfigure.serial = data->getNextEventSerial();
			event.xconfigure.display = data->displayAddress;
			data->putEvent(event);

			if (XServer::getServer()->trace) {
				BString log;
				log.append(data->displayId, 16);
				log += " Event";
				log += " ConfigureNotify";
				log += " win=";
				log.append(id, 16);
				log += " event=";
				log.append(parent->id, 16);
			}
			});
	}
	XServer::getServer()->iterateEventMask(id, StructureNotifyMask, [=](const DisplayDataPtr& data) {
		XEvent event = {};
		event.type = ConfigureNotify;
		event.xconfigure.event = id;
		event.xconfigure.window = id;
		event.xconfigure.x = x;
		event.xconfigure.y = y;
		event.xconfigure.width = width();
		event.xconfigure.height = height();
		event.xconfigure.border_width = border_width;
		event.xconfigure.above = above;
		event.xconfigure.override_redirect = attributes.override_redirect;
		event.xconfigure.serial = data->getNextEventSerial();
		event.xconfigure.display = data->displayAddress;
		data->putEvent(event);

		if (XServer::getServer()->trace) {
			BString log;
			log.append(data->displayId, 16);
			log += " Event";
			log += " ConfigureNotify";
			log += " win=";
			log.append(id, 16);
			log += " event=";
			log.append(id, 16);
		}
		});	
}

int XWindow::configure(U32 mask, XWindowChanges* changes) {
	bool sizeChanged = false;
	U32 width = this->width();
	U32 height = this->height();

	if (mask & CWX) {
		x = changes->x;
		sizeChanged = true;
	}
	if (mask & CWY) {
		y = changes->y;
		sizeChanged = true;
	}
	if (mask & CWWidth) {
		width = changes->width;
		sizeChanged = true;
	}
	if (mask & CWHeight) {
		height = changes->height;
		sizeChanged = true;
	}
	if (mask & CWBorderWidth) {
		border_width = changes->border_width;
	}
	if (mask & CWSibling) {
		kpanic("XReconfigureWMWindow CWSibling not handled");
	}
	if (mask & CWStackMode) {
		if (parent) {
			BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(parent->childrenMutex);
			int index = vectorIndexOf(parent->zchildren, shared_from_this());
			if (index >= 0 && index < parent->zchildren.size() - 1) {
				parent->zchildren.erase(parent->zchildren.begin() + index);
				parent->zchildren.push_back(shared_from_this());
			}
		}
	}
	if (sizeChanged) {
		if (width != this->width() || height != this->height()) {
			setSize(width, height);
		}
		KNativeWindowPtr nativeWindow = KNativeWindow::getNativeWindow();
		WndPtr win = nativeWindow->getWnd(id);
		if (win) {
			win->windowRect.left = x;
			win->windowRect.top = y;
			win->windowRect.right = x + width;
			win->windowRect.bottom = y + height;
		}
	}
	configureNotify();
	return Success;
}

bool XWindow::isDialog() {
	U32 type = NET_WM_WINDOW_TYPE();
	if (type == _NET_WM_WINDOW_TYPE_DIALOG || (type == 0 && WM_TRANSIENT_FOR() && !attributes.override_redirect)) {
		return true;
	}
	return false;
}

U32 XWindow::NET_WM_WINDOW_TYPE() {
	XPropertyPtr prop = getProperty(_NET_WM_WINDOW_TYPE);
	if (prop && prop->length) {
		return *(U32*)prop->value;
	}
	return 0;
}

U32 XWindow::WM_TRANSIENT_FOR() {
	XPropertyPtr prop = getProperty(XA_WM_TRANSIENT_FOR);
	if (prop && prop->length) {
		return *(U32*)prop->value;
	}
	return 0;
}

XWindowPtr XWindow::getWindowFromPoint(S32 x, S32 y) {
	for (int i = (int)zchildren.size() - 1; i>=0; i--) {
		const XWindowPtr& child = zchildren.at(i);
		if (!child->isMapped) {
			continue;
		}
		if (x >= child->x && x < child->x + (S32)child->width() && y >= child->y && y < child->y + (S32)child->height()) {
			x -= child->x;
			y -= child->y;
			XWindowPtr result = child->getWindowFromPoint(x, y);
			if (result) {
				return result;
			}
			return child;
		}
	}
	return shared_from_this();
}

void XWindow::motionNotify(const DisplayDataPtr& data, S32 x, S32 y) {
	S32 window_x = x;
	S32 window_y = y;
	screenToWindow(window_x, window_y);

	// winex11 doesn't seem to use subwindow
	XEvent event = {};
	event.type = MotionNotify;
	event.xmotion.serial = data->getNextEventSerial();
	event.xmotion.display = data->displayAddress;
	event.xmotion.window = id;
	event.xmotion.root = data->root;
	event.xmotion.subwindow = 0;
	event.xmotion.time = XServer::getServer()->getEventTime();
	event.xmotion.x = window_x;
	event.xmotion.y = window_y;
	event.xmotion.x_root = x;
	event.xmotion.y_root = y;
	event.xmotion.state = XServer::getServer()->getInputModifiers();
	event.xmotion.is_hint = NotifyNormal;
	event.xmotion.same_screen = True;
	data->putEvent(event);
}

void XWindow::mouseMoveScreenCoords(S32 x, S32 y) {		
	XServer::getServer()->iterateEventMask(id, PointerMotionMask, [=](const DisplayDataPtr& data) {
		motionNotify(data, x, y);
		});
}

void XWindow::getAncestorTree(std::vector<XWindowPtr>& ancestors) {
	XWindowPtr w = shared_from_this();
	while (w) {
		ancestors.push_back(w);
		w = w->parent;
	}
}

XWindowPtr XWindow::getLeastCommonAncestor(const XWindowPtr& wnd) {
	if (wnd->parent && wnd->parent->id == id) {
		return wnd->parent;
	}
	if (parent && parent->id == wnd->id) {
		return wnd;
	}
	if (wnd->id == id) {
		return wnd;
	}
	std::vector<XWindowPtr> tree1;
	std::vector<XWindowPtr> tree2;

	getAncestorTree(tree1);
	wnd->getAncestorTree(tree2);

	
	XWindowPtr result = XServer::getServer()->getRoot();
	for (int i = 0; i < tree1.size() && i < tree2.size(); i++) {
		if (tree1.at(i)->id == tree2.at(i)->id) {
			result = tree1.at(i);
		}
	}
	return result;
}

bool XWindow::doesThisOrAncestorHaveFocus() {
	XWindowPtr w = shared_from_this();
	XWindowPtr focus = XServer::getServer()->inputFocus;

	if (!focus) {
		return false;
	}
	while (w) {
		if (w->id == focus->id) {
			return true;
		}
		w = w->parent;
	}
	return false;
}

void XWindow::crossingNotify(const DisplayDataPtr& data, bool in, S32 x, S32 y, S32 mode, S32 detail) {
	S32 window_x = x;
	S32 window_y = y;
	screenToWindow(window_x, window_y);

	XEvent event = {};
	event.type = in ? EnterNotify : LeaveNotify;
	event.xcrossing.serial = data->getNextEventSerial();
	event.xcrossing.display = data->displayAddress;
	event.xcrossing.window = id;
	event.xcrossing.root = data->root;
	event.xcrossing.subwindow = 0;
	event.xcrossing.time = XServer::getServer()->getEventTime();
	event.xcrossing.x = window_x;
	event.xcrossing.y = window_y;
	event.xcrossing.x_root = x;
	event.xcrossing.y_root = y;
	event.xcrossing.mode = mode;
	event.xcrossing.detail = detail;
	event.xcrossing.same_screen = True;
	event.xcrossing.focus = doesThisOrAncestorHaveFocus() ? True : False;
	event.xcrossing.state = XServer::getServer()->getInputModifiers();
	data->putEvent(event);

	if (XServer::getServer()->trace) {
		BString log;
		log.append(data->displayId, 16);
		log += " Event";
		log += in ? " EnterNotify" : "LeaveNotify";
		log += " win=";
		log.append(id, 16);
		klog(log.c_str());
	}
}

void XWindow::focusOut() {
	XServer::getServer()->iterateEventMask(id, FocusChangeMask, [=](const DisplayDataPtr& data) {
		focusNotify(data, false, NotifyNormal, NotifyDetailNone);
		});
}

void XWindow::focusIn() {
	XServer::getServer()->iterateEventMask(id, FocusChangeMask, [=](const DisplayDataPtr& data) {
		focusNotify(data, true, NotifyNormal, NotifyDetailNone);
		});
}

void XWindow::focusNotify(const DisplayDataPtr& data, bool isIn, S32 mode, S32 detail) {
	XEvent event = {};
	event.type = isIn ? FocusIn : FocusOut;
	event.xfocus.serial = data->getNextEventSerial();
	event.xfocus.display = data->displayAddress;
	event.xfocus.window = id;
	event.xfocus.mode = mode;
	event.xfocus.detail = detail;
	data->putEvent(event);

	if (XServer::getServer()->trace) {
		BString log;
		log.append(data->displayId, 16);
		log += " Event";
		log += isIn ? " FocusIn" : "FocusOut";
		log += " win=";
		log.append(id, 16);
		klog(log.c_str());
	}
}

void XWindow::buttonNotify(const DisplayDataPtr& data, U32 button, S32 x, S32 y, bool pressed) {
	S32 window_x = x;
	S32 window_y = y;
	screenToWindow(window_x, window_y);

	// winex11 doesn't seem to use subwindow
	XEvent event = {};
	event.type = pressed ? ButtonPress : ButtonRelease;
	event.xbutton.serial = data->getNextEventSerial();
	event.xbutton.display = data->displayAddress;
	event.xbutton.window = id;
	event.xbutton.root = data->root;
	event.xbutton.subwindow = 0;
	event.xbutton.time = XServer::getServer()->getEventTime();
	event.xbutton.x = window_x;
	event.xbutton.y = window_y;
	event.xbutton.x_root = x;
	event.xbutton.y_root = y;
	event.xbutton.state = XServer::getServer()->getInputModifiers();
	event.xbutton.button = button;
	event.xbutton.same_screen = True;
	data->putEvent(event);

	// The state member is set to indicate the logical state of the pointer buttons and modifier keys just prior to the event
	if (pressed) {
		event.xbutton.state &= ~((Button1Mask) << (button - 1));
	} else {
		event.xbutton.state |= ((Button1Mask) << (button - 1));
	}
	if (XServer::getServer()->trace) {
		BString log;
		log.append(data->displayId, 16);
		log += " Event";
		log += pressed ? " ButtonPress" : " ButtonRelease";
		log += " button=";
		log += button;
		log += " root=";
		log.append(XServer::getServer()->getRoot()->id, 16);
		log += " event=";
		log.append(id, 16);
		klog(log.c_str());
	}
}

void XWindow::mouseButtonScreenCoords(U32 button, S32 x, S32 y, bool pressed) {
	
	XServer::getServer()->iterateEventMask(id, pressed ? ButtonPressMask : ButtonReleaseMask, [=](const DisplayDataPtr& data) {
		buttonNotify(data, button, x, y, pressed);
		});
}