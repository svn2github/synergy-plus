/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2003 Chris Schoeneman
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

#include "CScreen.h"
#include "IPlatformScreen.h"
#include "ProtocolTypes.h"
#include "CLog.h"
#include "IEventQueue.h"

//
// CScreen
//

CScreen::CScreen(IPlatformScreen* platformScreen) :
	m_screen(platformScreen),
	m_isPrimary(platformScreen->isPrimary()),
	m_enabled(false),
	m_entered(m_isPrimary),
	m_screenSaverSync(true),
	m_toggleKeys(0)
{
	assert(m_screen != NULL);

	// reset options
	resetOptions();

	LOG((CLOG_DEBUG "opened display"));
}

CScreen::~CScreen()
{
	if (m_enabled) {
		disable();
	}
	assert(!m_enabled);
	assert(m_entered == m_isPrimary);
	delete m_screen;
	LOG((CLOG_DEBUG "closed display"));
}

void
CScreen::enable()
{
	assert(!m_enabled);

	m_screen->enable();
	if (m_isPrimary) {
		enablePrimary();
	}
	else {
		enableSecondary();
	}

	// note activation
	m_enabled = true;
}

void
CScreen::disable()
{
	assert(m_enabled);

	if (!m_isPrimary && m_entered) {
		leave();
	}
	else if (m_isPrimary && !m_entered) {
		enter(0);
	}
	m_screen->disable();
	if (m_isPrimary) {
		disablePrimary();
	}
	else {
		disableSecondary();
	}

	// note deactivation
	m_enabled = false;
}

void
CScreen::enter(KeyModifierMask toggleMask)
{
	assert(m_entered == false);
	LOG((CLOG_INFO "entering screen"));

	// now on screen
	m_entered = true;

	m_screen->enter();
	if (m_isPrimary) {
		enterPrimary();
	}
	else {
		enterSecondary(toggleMask);
	}
}

bool
CScreen::leave()
{
	assert(m_entered == true);
	LOG((CLOG_INFO "leaving screen"));

	if (!m_screen->leave()) {
		return false;
	}
	if (m_isPrimary) {
		leavePrimary();
	}
	else {
		leaveSecondary();
	}

	// make sure our idea of clipboard ownership is correct
	m_screen->checkClipboards();

	// now not on screen
	m_entered = false;

	return true;
}

void
CScreen::reconfigure(UInt32 activeSides)
{
	assert(m_isPrimary);
	m_screen->reconfigure(activeSides);
}

void
CScreen::warpCursor(SInt32 x, SInt32 y)
{
	assert(m_isPrimary);
	m_screen->warpCursor(x, y);
}

void
CScreen::setClipboard(ClipboardID id, const IClipboard* clipboard)
{
	m_screen->setClipboard(id, clipboard);
}

void
CScreen::grabClipboard(ClipboardID id)
{
	m_screen->setClipboard(id, NULL);
}

void
CScreen::screensaver(bool activate)
{
	if (!m_isPrimary) {
		// activate/deactivation screen saver iff synchronization enabled
		if (m_screenSaverSync) {
			m_screen->screensaver(activate);
		}
	}
}

void
CScreen::keyDown(KeyID id, KeyModifierMask mask, KeyButton button)
{
	assert(!m_isPrimary);

	// check for ctrl+alt+del emulation
	if (id == kKeyDelete &&
		(mask & (KeyModifierControl | KeyModifierAlt)) ==
				(KeyModifierControl | KeyModifierAlt)) {
		LOG((CLOG_DEBUG "emulating ctrl+alt+del press"));
		if (m_screen->fakeCtrlAltDel()) {
			return;
		}
	}
	m_screen->fakeKeyDown(id, mask, button);
}

void
CScreen::keyRepeat(KeyID id,
				KeyModifierMask mask, SInt32 count, KeyButton button)
{
	assert(!m_isPrimary);
	m_screen->fakeKeyRepeat(id, mask, count, button);
}

void
CScreen::keyUp(KeyID, KeyModifierMask, KeyButton button)
{
	assert(!m_isPrimary);
	m_screen->fakeKeyUp(button);
}

void
CScreen::mouseDown(ButtonID button)
{
	assert(!m_isPrimary);
	m_screen->fakeMouseButton(button, true);
}

void
CScreen::mouseUp(ButtonID button)
{
	assert(!m_isPrimary);
	m_screen->fakeMouseButton(button, false);
}

void
CScreen::mouseMove(SInt32 x, SInt32 y)
{
	assert(!m_isPrimary);
	m_screen->fakeMouseMove(x, y);
}

void
CScreen::mouseRelativeMove(SInt32 dx, SInt32 dy)
{
	assert(!m_isPrimary);
	m_screen->fakeMouseRelativeMove(dx, dy);
}

void
CScreen::mouseWheel(SInt32 delta)
{
	assert(!m_isPrimary);
	m_screen->fakeMouseWheel(delta);
}

void
CScreen::resetOptions()
{
	// reset options
	m_halfDuplex = 0;

	// if screen saver synchronization was off then turn it on since
	// that's the default option state.
	if (!m_screenSaverSync) {
		m_screenSaverSync = true;
		if (!m_isPrimary) {
			m_screen->openScreensaver(false);
		}
	}

	// let screen handle its own options
	m_screen->resetOptions();
}

void
CScreen::setOptions(const COptionsList& options)
{
	// update options
	bool oldScreenSaverSync = m_screenSaverSync;
	for (UInt32 i = 0, n = options.size(); i < n; i += 2) {
		if (options[i] == kOptionScreenSaverSync) {
			m_screenSaverSync = (options[i + 1] != 0);
			LOG((CLOG_DEBUG1 "screen saver synchronization %s", m_screenSaverSync ? "on" : "off"));
		}
		else if (options[i] == kOptionHalfDuplexCapsLock) {
			if (options[i + 1] != 0) {
				m_halfDuplex |=  KeyModifierCapsLock;
			}
			else {
				m_halfDuplex &= ~KeyModifierCapsLock;
			}
			m_screen->setHalfDuplexMask(m_halfDuplex);
			LOG((CLOG_DEBUG1 "half-duplex caps-lock %s", ((m_halfDuplex & KeyModifierCapsLock) != 0) ? "on" : "off"));
		}
		else if (options[i] == kOptionHalfDuplexNumLock) {
			if (options[i + 1] != 0) {
				m_halfDuplex |=  KeyModifierNumLock;
			}
			else {
				m_halfDuplex &= ~KeyModifierNumLock;
			}
			m_screen->setHalfDuplexMask(m_halfDuplex);
			LOG((CLOG_DEBUG1 "half-duplex num-lock %s", ((m_halfDuplex & KeyModifierNumLock) != 0) ? "on" : "off"));
		}
		else if (options[i] == kOptionHalfDuplexScrollLock) {
			if (options[i + 1] != 0) {
				m_halfDuplex |=  KeyModifierScrollLock;
			}
			else {
				m_halfDuplex &= ~KeyModifierScrollLock;
			}
			m_screen->setHalfDuplexMask(m_halfDuplex);
			LOG((CLOG_DEBUG1 "half-duplex scroll-lock %s", ((m_halfDuplex & KeyModifierScrollLock) != 0) ? "on" : "off"));
		}
	}

	// update screen saver synchronization
	if (!m_isPrimary && oldScreenSaverSync != m_screenSaverSync) {
		if (m_screenSaverSync) {
			m_screen->openScreensaver(false);
		}
		else {
			m_screen->closeScreensaver();
		}
	}

	// let screen handle its own options
	m_screen->setOptions(options);
}

void
CScreen::setSequenceNumber(UInt32 seqNum)
{
	m_screen->setSequenceNumber(seqNum);
}

bool
CScreen::isOnScreen() const
{
	return m_entered;
}

bool
CScreen::isLockedToScreen() const
{
	// check for pressed mouse buttons
	if (m_screen->isAnyMouseButtonDown()) {
		LOG((CLOG_DEBUG "locked by mouse button"));
		return true;
	}

// note -- we don't lock to the screen if a key is down.  key
// reporting is simply not reliable enough to trust.  the effect
// of switching screens with a key down is that the client will
// receive key repeats and key releases for keys that it hasn't
// see go down.  that's okay because CKeyState will ignore those
// events.  the user might be surprised that any modifier keys
// held while crossing to another screen don't apply on the
// target screen.  if that ends up being a problem we can try
// to synthesize a key press for those modifiers on entry.
/*
	// check for any pressed key
	KeyButton key = isAnyKeyDown();
	if (key != 0) {
		// double check current state of the keys.  this shouldn't
		// be necessary but we don't seem to get some key release
		// events sometimes.  this is an emergency backup so the
		// client doesn't get stuck on the screen.
		m_screen->updateKeys();
		KeyButton key2 = isAnyKeyDown();
		if (key2 != 0) {
			LOG((CLOG_DEBUG "locked by %s", m_screen->getKeyName(key2)));
			return true;
		}
		else {
			LOG((CLOG_DEBUG "spuriously locked by %s", m_screen->getKeyName(key)));
		}
	}
*/

	// not locked
	return false;
}

SInt32
CScreen::getJumpZoneSize() const
{
	if (!m_isPrimary) {
		return 0;
	}
	else {
		return m_screen->getJumpZoneSize();
	}
}

void
CScreen::getCursorCenter(SInt32& x, SInt32& y) const
{
	m_screen->getCursorCenter(x, y);
}

KeyModifierMask
CScreen::getActiveModifiers() const
{
	return m_screen->getActiveModifiers();
}

KeyModifierMask
CScreen::pollActiveModifiers() const
{
	return m_screen->pollActiveModifiers();
}

void*
CScreen::getEventTarget() const
{
	return m_screen;
}

bool
CScreen::getClipboard(ClipboardID id, IClipboard* clipboard) const
{
	return m_screen->getClipboard(id, clipboard);
}

void
CScreen::getShape(SInt32& x, SInt32& y, SInt32& w, SInt32& h) const
{
	m_screen->getShape(x, y, w, h);
}

void
CScreen::getCursorPos(SInt32& x, SInt32& y) const
{
	m_screen->getCursorPos(x, y);
}

void
CScreen::enablePrimary()
{
	// get notified of screen saver activation/deactivation
	m_screen->openScreensaver(true);

	// claim screen changed size
	EVENTQUEUE->addEvent(CEvent(getShapeChangedEvent(), getEventTarget()));
}

void
CScreen::enableSecondary()
{
	// assume primary has all clipboards
	for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
		grabClipboard(id);
	}

	// disable the screen saver if synchronization is enabled
	if (m_screenSaverSync) {
		m_screen->openScreensaver(false);
	}
}

void
CScreen::disablePrimary()
{
	// done with screen saver
	m_screen->closeScreensaver();
}

void
CScreen::disableSecondary()
{
	// done with screen saver
	m_screen->closeScreensaver();
}

void
CScreen::enterPrimary()
{
	// do nothing
}

void
CScreen::enterSecondary(KeyModifierMask toggleMask)
{
	// remember toggle key state.  we'll restore this when we leave.
	m_toggleKeys = getActiveModifiers();

	// restore toggle key state
	setToggleState(toggleMask);
}

void
CScreen::leavePrimary()
{
	// we don't track keys while on the primary screen so update our
	// idea of them now.  this is particularly to update the state of
	// the toggle modifiers.
	m_screen->updateKeys();
}

void
CScreen::leaveSecondary()
{
	// release any keys we think are still down
	releaseKeys();

	// restore toggle key state
	setToggleState(m_toggleKeys);
}

void
CScreen::releaseKeys()
{
	// release keys that we've synthesized a press for and only those
	// keys.  we don't want to synthesize a release on a key the user
	// is still physically pressing.
	for (KeyButton i = 1; i < IKeyState::kNumButtons; ++i) {
		if (m_screen->isServerKeyDown(i)) {
			m_screen->fakeKeyUp(i);
		}
	}
}

void
CScreen::setToggleState(KeyModifierMask mask)
{
	// toggle modifiers that don't match the desired state
	KeyModifierMask different = (m_screen->getActiveModifiers() ^ mask);
	if ((different & KeyModifierCapsLock)   != 0) {
		m_screen->fakeToggle(KeyModifierCapsLock);
	}
	if ((different & KeyModifierNumLock)    != 0) {
		m_screen->fakeToggle(KeyModifierNumLock);
	}
	if ((different & KeyModifierScrollLock) != 0) {
		m_screen->fakeToggle(KeyModifierScrollLock);
	}
}

KeyButton
CScreen::isAnyKeyDown() const
{
	for (KeyButton i = 1; i < IKeyState::kNumButtons; ++i) {
		if (m_screen->isKeyDown(i)) {
			return i;
		}
	}
	return 0;
}
