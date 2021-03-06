/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2004 Chris Schoeneman
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "COSXScreen.h"
#include "COSXClipboard.h"
#include "COSXEventQueueBuffer.h"
#include "COSXKeyState.h"
#include "COSXScreenSaver.h"
#include "CClipboard.h"
#include "CLog.h"
#include "IEventQueue.h"
#include "TMethodEventJob.h"

//
// COSXScreen
//

COSXScreen::COSXScreen(bool isPrimary) :
	m_isPrimary(isPrimary),
	m_isOnScreen(m_isPrimary),
	m_cursorPosValid(false),
	m_cursorHidden(false),
	m_keyState(NULL),
	m_sequenceNumber(0),
	m_screensaver(NULL),
	m_screensaverNotify(false),
	m_ownClipboard(false),
	m_hiddenWindow(NULL),
	m_userInputWindow(NULL),
	m_displayManagerNotificationUPP(NULL)
{
	try {
		m_displayID = CGMainDisplayID();
		updateScreenShape();
		m_screensaver = new COSXScreenSaver();
		m_keyState    = new COSXKeyState();

		if (m_isPrimary) {
			// 1x1 window (to minimze the back buffer allocated for this
			// window.
			Rect bounds = { 100, 100, 101, 101 };

			// m_hiddenWindow is a window meant to let us get mouse moves
			// when the focus is on another computer.  If you get your event
			// from the application event target you'll get every mouse
			// moves. On the other hand the Window event target will only
			// get events when the mouse moves over the window. 
			
			// The ignoreClicks attributes makes it impossible for the
			// user to click on our invisible window. 
			CreateNewWindow(kUtilityWindowClass, 
							kWindowNoShadowAttribute |
							kWindowIgnoreClicksAttribute |
							kWindowNoActivatesAttribute, 
							&bounds, &m_hiddenWindow);
			
			// Make it invisible
			SetWindowAlpha(m_hiddenWindow, 0);
			ShowWindow(m_hiddenWindow);

			// m_userInputWindow is a window meant to let us get mouse moves
			// when the focus is on this computer. 
			Rect inputBounds = { 100, 100, 200, 200 };
			CreateNewWindow(kUtilityWindowClass, 
							kWindowNoShadowAttribute |
							kWindowOpaqueForEventsAttribute |
							kWindowStandardHandlerAttribute, 
							&inputBounds, &m_userInputWindow);

			SetWindowAlpha(m_userInputWindow, 0);
		}
		
		m_displayManagerNotificationUPP =
			NewDMExtendedNotificationUPP(displayManagerCallback);

		OSStatus err = GetCurrentProcess(&m_PSN);

		err = DMRegisterExtendedNotifyProc(m_displayManagerNotificationUPP,
							this, 0, &m_PSN);
	}
	catch (...) {
		DMRemoveExtendedNotifyProc(m_displayManagerNotificationUPP,
							NULL, &m_PSN, 0);
		if (m_hiddenWindow) {
			ReleaseWindow(m_hiddenWindow);
			m_hiddenWindow = NULL;
		}

		if (m_userInputWindow) {
			ReleaseWindow(m_userInputWindow);
			m_userInputWindow = NULL;
		}
		delete m_keyState;
		delete m_screensaver;
		throw;
	}

	// install event handlers
	EVENTQUEUE->adoptHandler(CEvent::kSystem, IEventQueue::getSystemTarget(),
							new TMethodEventJob<COSXScreen>(this,
								&COSXScreen::handleSystemEvent));

	// install the platform event queue
	EVENTQUEUE->adoptBuffer(new COSXEventQueueBuffer);
}

COSXScreen::~COSXScreen()
{
	disable();
	EVENTQUEUE->adoptBuffer(NULL);
	EVENTQUEUE->removeHandler(CEvent::kSystem, IEventQueue::getSystemTarget());

	if (m_hiddenWindow) {
		ReleaseWindow(m_hiddenWindow);
		m_hiddenWindow = NULL;
	}

	if (m_userInputWindow) {
		ReleaseWindow(m_userInputWindow);
		m_userInputWindow = NULL;
	}

	DMRemoveExtendedNotifyProc(m_displayManagerNotificationUPP,
							NULL, &m_PSN, 0);
	
	delete m_keyState;
	delete m_screensaver;
}

void*
COSXScreen::getEventTarget() const
{
	return const_cast<COSXScreen*>(this);
}

bool
COSXScreen::getClipboard(ClipboardID, IClipboard* dst) const
{
	COSXClipboard src;
	CClipboard::copy(dst, &src);
	return true;
}

void
COSXScreen::getShape(SInt32& x, SInt32& y, SInt32& w, SInt32& h) const
{
	x = m_x;
	y = m_y;
	w = m_w;
	h = m_h;
}

void
COSXScreen::getCursorPos(SInt32& x, SInt32& y) const
{
	Point mouse;
	GetGlobalMouse(&mouse);
	x                = mouse.h;
	y                = mouse.v;
	m_cursorPosValid = true;
	m_xCursor        = x;
	m_yCursor        = y;
}

void
COSXScreen::reconfigure(UInt32 activeSides)
{
	// FIXME
	(void)activeSides;
}

void
COSXScreen::warpCursor(SInt32 x, SInt32 y)
{
	// move cursor without generating events
	CGPoint pos;
	pos.x = x;
	pos.y = y;
	CGWarpMouseCursorPosition(pos);

	// save new cursor position
	m_xCursor        = x;
	m_yCursor        = y;
	m_cursorPosValid = true;
}

SInt32
COSXScreen::getJumpZoneSize() const
{
	// FIXME -- is this correct?
	return 1;
}

bool
COSXScreen::isAnyMouseButtonDown() const
{
	// FIXME
	return false;
}

void
COSXScreen::getCursorCenter(SInt32& x, SInt32& y) const
{
	x = m_xCenter;
	y = m_yCenter;
}

void
COSXScreen::postMouseEvent(const CGPoint & pos) const
{
	// synthesize event.  CGPostMouseEvent is a particularly good
	// example of a bad API.  we have to shadow the mouse state to
	// use this API and if we want to support more buttons we have
	// to recompile.
	//
	// the order of buttons on the mac is:
	// 1 - Left
	// 2 - Right
	// 3 - Middle
	// Whatever the USB device defined.
	//
	// It is a bit weird that the behaviour of buttons over 3 are dependent
	// on currently plugged in USB devices.
	CGPostMouseEvent(pos, true, sizeof(m_buttons) / sizeof(m_buttons[0]),
				m_buttons[0],
				m_buttons[2],
				m_buttons[1],
				m_buttons[3],
				m_buttons[4]);
}


void
COSXScreen::fakeMouseButton(ButtonID id, bool press) const
{
	// get button index
	UInt32 index = id - kButtonLeft;
	if (index >= sizeof(m_buttons) / sizeof(m_buttons[0])) {
		return;
	}

	// update state
	m_buttons[index] = press;

	CGPoint pos;
	if (!m_cursorPosValid) {
		SInt32 x, y;
		getCursorPos(x, y);
	}
	pos.x = m_xCursor;
	pos.y = m_yCursor;
	postMouseEvent(pos);
}

void
COSXScreen::fakeMouseMove(SInt32 x, SInt32 y) const
{
	// synthesize event
	CGPoint pos;
	pos.x = x;
	pos.y = y;
	postMouseEvent(pos);

	// save new cursor position
	m_xCursor        = x;
	m_yCursor        = y;
	m_cursorPosValid = true;
}

void
COSXScreen::fakeMouseRelativeMove(SInt32 dx, SInt32 dy) const
{
	// OS X does not appear to have a fake relative mouse move function.
	// simulate it by getting the current mouse position and adding to
	// that.  this can yield the wrong answer but there's not much else
	// we can do.

	// get current position
	Point oldPos;
	GetGlobalMouse(&oldPos);

	// synthesize event
	CGPoint pos;
	pos.x = oldPos.h + dx;
	pos.y = oldPos.v + dy;
	postMouseEvent(pos);

	// we now assume we don't know the current cursor position
	m_cursorPosValid = false;
}

void
COSXScreen::fakeMouseWheel(SInt32 delta) const
{
	// synergy uses a wheel step size of 120.  the mac uses a step size of 1.
	delta /= 120;
	if (delta == 0) {
		return;
	}

	CFPropertyListRef pref = ::CFPreferencesCopyValue(
							CFSTR("com.apple.scrollwheel.scaling") , 
							kCFPreferencesAnyApplication, 
							kCFPreferencesCurrentUser,
							kCFPreferencesAnyHost);

	int32_t wheelIncr = 1;

	if (pref != NULL) {
		CFTypeID id = CFGetTypeID(pref);
		if (id == CFNumberGetTypeID()) {
			CFNumberRef value = static_cast<CFNumberRef>(pref);

			double scaling;
			if (CFNumberGetValue(value, kCFNumberDoubleType, &scaling)) {
				wheelIncr = (int32_t)(8 * scaling);
				if (wheelIncr == 0) {
					wheelIncr = 1;
				}
			}
		}
		CFRelease(pref);
	}

	// note that we ignore the magnitude of the delta.  i think this is to
	// avoid local wheel acceleration.
	if (delta < 0) {
		wheelIncr = -wheelIncr;
	}

	CGPostScrollWheelEvent(1, wheelIncr);
}

void
COSXScreen::enable()
{
	// FIXME -- install clipboard snooper (if we need one)

	if (m_isPrimary) {
		// FIXME -- start watching jump zones
	}
	else {
		// FIXME -- prevent system from entering power save mode

		// hide cursor
		if (!m_cursorHidden) {
			CGDisplayHideCursor(m_displayID);
			m_cursorHidden = true;
		}

		// warp the mouse to the cursor center
		fakeMouseMove(m_xCenter, m_yCenter);

		// FIXME -- prepare to show cursor if it moves
	}

	updateKeys();
}

void
COSXScreen::disable()
{
	if (m_isPrimary) {
		// FIXME -- stop watching jump zones, stop capturing input
	}
	else {
		// show cursor
		if (m_cursorHidden) {
			CGDisplayShowCursor(m_displayID);
			m_cursorHidden = false;
		}

		// FIXME -- allow system to enter power saving mode
	}

	// FIXME -- uninstall clipboard snooper (if we needed one)

	m_isOnScreen = m_isPrimary;
}

void
COSXScreen::enter()
{
	if (m_isPrimary) {
		// stop capturing input, watch jump zones
		HideWindow( m_userInputWindow );
		ShowWindow( m_hiddenWindow );

		SetMouseCoalescingEnabled(true, NULL);

		CGSetLocalEventsSuppressionInterval(HUGE_VAL);
	}
	else {
		// show cursor
		if (m_cursorHidden) {
			CGDisplayShowCursor(m_displayID);
			m_cursorHidden = false;
		}

		// reset buttons
		for (UInt32 i = 0; i < sizeof(m_buttons) / sizeof(m_buttons[0]); ++i) {
			m_buttons[i] = false;
		}
	}

	// now on screen
	m_isOnScreen = true;
}

bool
COSXScreen::leave()
{
	// FIXME -- choose keyboard layout if per-process and activate it here

	if (m_isPrimary) {
		// update key and button state
		updateKeys();

		// warp to center
		warpCursor(m_xCenter, m_yCenter);

		// capture events
		HideWindow(m_hiddenWindow);
		ShowWindow(m_userInputWindow);
		RepositionWindow(m_userInputWindow,
							m_userInputWindow, kWindowCenterOnMainScreen);
		SetUserFocusWindow(m_userInputWindow);

		// The OS will coalesce some events if they are similar enough in a
		// short period of time this is bad for us since we need every event
		// to send it over to other machines.  So disable it.		
		SetMouseCoalescingEnabled(false, NULL);
		CGSetLocalEventsSuppressionInterval(0.0001);
	}
	else {
		// hide cursor
		if (!m_cursorHidden) {
			CGDisplayHideCursor(m_displayID);
			m_cursorHidden = true;
		}

		// warp the mouse to the cursor center
		fakeMouseMove(m_xCenter, m_yCenter);

		// FIXME -- prepare to show cursor if it moves

		// take keyboard focus	
		// FIXME
	}

	// now off screen
	m_isOnScreen = false;

	return true;
}

bool
COSXScreen::setClipboard(ClipboardID, const IClipboard* src)
{
	COSXClipboard dst;
	m_ownClipboard = true;
	if (src != NULL) {
		// save clipboard data
		return CClipboard::copy(&dst, src);
	}
	else {
		// assert clipboard ownership
		if (!dst.open(0)) {
			return false;
		}
		dst.empty();
		dst.close();
		return true;
	}
}

void
COSXScreen::checkClipboards()
{
	if (m_ownClipboard && !COSXClipboard::isOwnedBySynergy()) {
		static ScrapRef sScrapbook = NULL;
		ScrapRef currentScrap;
		GetCurrentScrap(&currentScrap);

		if (sScrapbook != currentScrap) {
			m_ownClipboard = false;
			sendClipboardEvent(getClipboardGrabbedEvent(), kClipboardClipboard);
			sendClipboardEvent(getClipboardGrabbedEvent(), kClipboardSelection);
			sScrapbook = currentScrap;
		}
	}
}

void
COSXScreen::openScreensaver(bool notify)
{
	m_screensaverNotify = notify;
	if (!m_screensaverNotify) {
		m_screensaver->disable();
	}
}

void
COSXScreen::closeScreensaver()
{
	if (!m_screensaverNotify) {
		m_screensaver->enable();
	}
}

void
COSXScreen::screensaver(bool activate)
{
	if (activate) {
		m_screensaver->activate();
	}
	else {
		m_screensaver->deactivate();
	}
}

void
COSXScreen::resetOptions()
{
	// no options
}

void
COSXScreen::setOptions(const COptionsList&)
{
	// no options
}

void
COSXScreen::setSequenceNumber(UInt32 seqNum)
{
	m_sequenceNumber = seqNum;
}

bool
COSXScreen::isPrimary() const
{
	return m_isPrimary;
}

void
COSXScreen::sendEvent(CEvent::Type type, void* data) const
{
	EVENTQUEUE->addEvent(CEvent(type, getEventTarget(), data));
}

void
COSXScreen::sendClipboardEvent(CEvent::Type type, ClipboardID id) const
{
	CClipboardInfo* info   = (CClipboardInfo*)malloc(sizeof(CClipboardInfo));
	info->m_id             = id;
	info->m_sequenceNumber = m_sequenceNumber;
	sendEvent(type, info);
}

void
COSXScreen::handleSystemEvent(const CEvent& event, void*)
{
	EventRef* carbonEvent = reinterpret_cast<EventRef*>(event.getData());
	assert(carbonEvent != NULL);

	UInt32 eventClass = GetEventClass(*carbonEvent);
	
	switch (eventClass) {
	case kEventClassMouse:
		switch (GetEventKind(*carbonEvent)) {
		case kEventMouseDown:
		{
			UInt16 myButton;
			GetEventParameter(*carbonEvent,
					kEventParamMouseButton,
					typeMouseButton,
					NULL,
					sizeof(myButton),
					NULL,
					&myButton);
			onMouseButton(true, myButton);
			break;
		}

		case kEventMouseUp:
		{
			UInt16 myButton;
			GetEventParameter(*carbonEvent,
					kEventParamMouseButton,
					typeMouseButton,
					NULL,
					sizeof(myButton),
					NULL,
					&myButton);
			onMouseButton(false, myButton);
			break;
		}

		case kEventMouseDragged:
		case kEventMouseMoved:
		{
			HIPoint point;
			GetEventParameter(*carbonEvent,
					kEventParamMouseLocation,
					typeHIPoint,
					NULL,
					sizeof(point),
					NULL,
					&point);
			onMouseMove((SInt32)point.x, (SInt32)point.y);
			break;
		}

		case kEventMouseWheelMoved:
		{
			EventMouseWheelAxis axis;
			SInt32 delta;
			GetEventParameter(*carbonEvent,
					kEventParamMouseWheelAxis,
					typeMouseWheelAxis,
					NULL,
					sizeof(axis),
					NULL,
					&axis);
			if (axis == kEventMouseWheelAxisY) {
				GetEventParameter(*carbonEvent,
					kEventParamMouseWheelDelta,
					typeLongInteger,
					NULL,
					sizeof(delta),
					NULL,
					&delta);
				onMouseWheel(120 * (SInt32)delta);
			}
			break;
		}
		}
		break;

	case kEventClassKeyboard: 
		switch (GetEventKind(*carbonEvent)) {
		case kEventRawKeyUp:
		case kEventRawKeyDown:
		case kEventRawKeyRepeat:
		case kEventRawKeyModifiersChanged:
//		case kEventHotKeyPressed:
//		case kEventHotKeyReleased:
			onKey(*carbonEvent);
			break;
		}
		break;

	case kEventClassWindow:
		SendEventToWindow(*carbonEvent, m_userInputWindow);
		switch (GetEventKind(*carbonEvent)) {
		case kEventWindowActivated:
			LOG((CLOG_DEBUG1 "window activated"));
			break;

		case kEventWindowDeactivated:
			LOG((CLOG_DEBUG1 "window deactivated"));
			break;

		case kEventWindowFocusAcquired:
			LOG((CLOG_DEBUG1 "focus acquired"));
			break;

		case kEventWindowFocusRelinquish:
			LOG((CLOG_DEBUG1 "focus released"));
			break;
		}
		break;

	default: 
		break;
	}
}

bool 
COSXScreen::onMouseMove(SInt32 mx, SInt32 my)
{
	LOG((CLOG_DEBUG2 "mouse move %+d,%+d", mx, my));

	SInt32 x = mx - m_xCursor;
	SInt32 y = my - m_yCursor;

	if ((x == 0 && y == 0) || (mx == m_xCenter && mx == m_yCenter)) {
		return true;
	}

	// save position to compute delta of next motion
	m_xCursor = mx;
	m_yCursor = my;

	if (m_isOnScreen) {
		// motion on primary screen
		sendEvent(getMotionOnPrimaryEvent(),
							CMotionInfo::alloc(m_xCursor, m_yCursor));
	}
	else {
		// motion on secondary screen.  warp mouse back to
		// center.
		warpCursor(m_xCenter, m_yCenter);

		// examine the motion.  if it's about the distance
		// from the center of the screen to an edge then
		// it's probably a bogus motion that we want to
		// ignore (see warpCursorNoFlush() for a further
		// description).
		static SInt32 bogusZoneSize = 10;
		if (-x + bogusZoneSize > m_xCenter - m_x ||
			 x + bogusZoneSize > m_x + m_w - m_xCenter ||
			-y + bogusZoneSize > m_yCenter - m_y ||
			 y + bogusZoneSize > m_y + m_h - m_yCenter) {
			LOG((CLOG_DEBUG "dropped bogus motion %+d,%+d", x, y));
		}
		else {
			// send motion
			sendEvent(getMotionOnSecondaryEvent(), CMotionInfo::alloc(x, y));
		}
	}

	return true;
}

bool				
COSXScreen::onMouseButton(bool pressed, UInt16 macButton) const
{
	// Buttons 2 and 3 are inverted on the mac
	ButtonID button = mapMacButtonToSynergy(macButton);

	if (pressed) {
		LOG((CLOG_DEBUG1 "event: button press button=%d", button));
		if (button != kButtonNone) {
			sendEvent(getButtonDownEvent(), CButtonInfo::alloc(button));
		}
	}
	else {
		LOG((CLOG_DEBUG1 "event: button release button=%d", button));
		if (button != kButtonNone) {
			sendEvent(getButtonUpEvent(), CButtonInfo::alloc(button));
		}
	}
	
	return true;
}

bool
COSXScreen::onMouseWheel(SInt32 delta) const
{
	LOG((CLOG_DEBUG1 "event: button wheel delta=%d", delta));
	sendEvent(getWheelEvent(), CWheelInfo::alloc(delta));
	return true;
}

pascal void 
COSXScreen::displayManagerCallback(void* inUserData, SInt16 inMessage, void*)
{
	COSXScreen* screen = (COSXScreen*)inUserData;

	if (inMessage == kDMNotifyEvent) {
		screen->onDisplayChange();
	}
}

bool
COSXScreen::onDisplayChange()
{
	// screen resolution may have changed.  save old shape.
	SInt32 xOld = m_x, yOld = m_y, wOld = m_w, hOld = m_h;

	// update shape
	updateScreenShape();

	// do nothing if resolution hasn't changed
	if (xOld != m_x || yOld != m_y || wOld != m_w || hOld != m_h) {
		if (m_isPrimary) {
			// warp mouse to center if off screen
			if (!m_isOnScreen) {
				warpCursor(m_xCenter, m_yCenter);
			}
		}

		// send new screen info
		sendEvent(getShapeChangedEvent());
	}

	return true;
}


bool				
COSXScreen::onKey(EventRef event) const
{
	UInt32 eventKind = GetEventKind(event);

	// get the key
	UInt32 virtualKey;
	GetEventParameter(event, kEventParamKeyCode, typeUInt32,
							NULL, sizeof(virtualKey), NULL, &virtualKey);
	LOG((CLOG_DEBUG1 "event: Key event kind: %d, keycode=%d", eventKind, virtualKey));

	// sadly, OS X doesn't report the virtualKey for modifier keys.
	// virtualKey will be zero for modifier keys.  since that's not good
	// enough we'll have to figure out what the key was.
	if (virtualKey == 0 && eventKind == kEventRawKeyModifiersChanged) {
		// get old and new modifier state
		KeyModifierMask oldMask = getActiveModifiers();
		KeyModifierMask newMask = mapMacModifiersToSynergy(event);
		m_keyState->handleModifierKeys(getEventTarget(), oldMask, newMask);
		return true;
	}

	// decode event type
	bool down	  = (eventKind == kEventRawKeyDown);
	bool up		  = (eventKind == kEventRawKeyUp);
	bool isRepeat = (eventKind == kEventRawKeyRepeat);

	// map event to keys
	KeyModifierMask mask;
	COSXKeyState::CKeyIDs keys;
	KeyButton button = m_keyState->mapKeyFromEvent(keys, &mask, event);
	if (button == 0) {
		return false;
	}

	// update button state
	if (down) {
		m_keyState->setKeyDown(button, true);
	}
	else if (up) {
		if (!isKeyDown(button)) {
			// up event for a dead key.  throw it away.
			return false;
		}
		m_keyState->setKeyDown(button, false);
	}

	// send key events
	for (COSXKeyState::CKeyIDs::const_iterator i = keys.begin();
							i != keys.end(); ++i) {
		m_keyState->sendKeyEvent(getEventTarget(), down, isRepeat,
							*i, mask, 1, button);
	}

	return true;
}

ButtonID 
COSXScreen::mapMacButtonToSynergy(UInt16 macButton) const
{
	switch (macButton) {
	case 1:
		return kButtonLeft;

	case 2:
		return kButtonRight;

	case 3:
		return kButtonMiddle;
	}
	
	return kButtonNone;
}

KeyModifierMask
COSXScreen::mapMacModifiersToSynergy(EventRef event) const
{
	// get native bit mask
	UInt32 macMask;
	GetEventParameter(event, kEventParamKeyModifiers, typeUInt32,
							NULL, sizeof(macMask), NULL, &macMask);

	// convert
	KeyModifierMask outMask = 0;
	if ((macMask & shiftKey) != 0) {
		outMask |= KeyModifierShift;
	}
	if ((macMask & rightShiftKey) != 0) {
		outMask |= KeyModifierShift;
	}
	if ((macMask & controlKey) != 0) {
		outMask |= KeyModifierControl;
	}
	if ((macMask & rightControlKey) != 0) {
		outMask |= KeyModifierControl;
	}
	if ((macMask & cmdKey) != 0) {
		outMask |= KeyModifierAlt;
	}
	if ((macMask & optionKey) != 0) {
		outMask |= KeyModifierSuper;
	}
	if ((macMask & rightOptionKey) != 0) {
		outMask |= KeyModifierSuper;
	}
	if ((macMask & alphaLock) != 0) {
		outMask |= KeyModifierCapsLock;
	}

	return outMask;
}

void
COSXScreen::updateButtons()
{
	// FIXME -- get current button state into m_buttons[]
}

IKeyState*
COSXScreen::getKeyState() const
{
	return m_keyState;
}

void
COSXScreen::updateScreenShape()
{
	// get info for each display
	CGDisplayCount displayCount = 0;

	if (CGGetActiveDisplayList(0, NULL, &displayCount) != CGDisplayNoErr) {
		return;
	}
	
	if (displayCount == 0) {
		return;
	}

	CGDirectDisplayID* displays = new CGDirectDisplayID[displayCount];
	if (displays == NULL) {
		return;
	}

	if (CGGetActiveDisplayList(displayCount,
							displays, &displayCount) != CGDisplayNoErr) {
		delete[] displays;
		return;
	}

	// get smallest rect enclosing all display rects
	CGRect totalBounds = CGRectZero;
	for (CGDisplayCount i = 0; i < displayCount; ++i) {
		CGRect bounds = CGDisplayBounds(displays[i]);
		totalBounds   = CGRectUnion(totalBounds, bounds);
	}

	// get shape of default screen
	m_x = (SInt32)totalBounds.origin.x;
	m_y = (SInt32)totalBounds.origin.y;
	m_w = (SInt32)totalBounds.size.width;
	m_h = (SInt32)totalBounds.size.height;

	// get center of default screen
	GDHandle mainScreen = GetMainDevice();
	if (mainScreen != NULL) {
		const Rect& rect = (*mainScreen)->gdRect;
		m_xCenter = (rect.left + rect.right) / 2;
		m_yCenter = (rect.top + rect.bottom) / 2;
	}
	else {
		m_xCenter = m_x + (m_w >> 1);
		m_yCenter = m_y + (m_h >> 1);
	}

	delete[] displays;

	LOG((CLOG_DEBUG "screen shape: %d,%d %dx%d on %u %s", m_x, m_y, m_w, m_h, displayCount, (displayCount == 1) ? "display" : "displays"));
}
