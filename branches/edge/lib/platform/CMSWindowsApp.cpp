/*
* synergy -- mouse and keyboard sharing utility
* Copyright (C) 2002 Chris Schoeneman
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

#include "CMSWindowsApp.h"
#include "Version.h"
#include "CLog.h"
#include "XArchWindows.h"

#include <sstream>

CMSWindowsApp::CMSWindowsApp()
{
}

CMSWindowsApp::~CMSWindowsApp()
{
}

CString
CMSWindowsApp::getServiceArgs() const
{
	std::stringstream argBuf;
	for (int i = 1; i < __argc; i++) {
		const char* arg = __argv[i];

		// ignore service setup args
		if (_stricmp(arg, "--service") == 0) {
			// ignore and skip the next arg also (service action)
			i++;
		}
		else {
			argBuf << " " << __argv[i];
		}
	}
	return argBuf.str();
}

void
CMSWindowsApp::installService()
{
	CString args = getServiceArgs();

	// get the path of this program
	char thisPath[MAX_PATH];
	GetModuleFileName(m_instance, thisPath, MAX_PATH);

	ARCH->installDaemon(
		m_daemonName.c_str(), m_daemonInfo.c_str(), 
		thisPath, args.c_str(), NULL, true);

	LOG((CLOG_INFO "service '%s' installed with args: %s",
		m_daemonName.c_str(), args != "" ? args.c_str() : "none" ));
}

void
CMSWindowsApp::uninstallService()
{
	ARCH->uninstallDaemon(m_daemonName.c_str(), true);
	LOG((CLOG_INFO "service '%s' uninstalled", m_daemonName.c_str()));
}

void
CMSWindowsApp::startService()
{
	// open service manager
	SC_HANDLE mgr = OpenSCManager(NULL, NULL, GENERIC_READ);
	if (mgr == NULL) {
		throw XArchEvalWindows();
	}

	// open the service
	SC_HANDLE service = OpenService(
		mgr, m_daemonName.c_str(), SERVICE_START);

	if (service == NULL) {
		CloseServiceHandle(mgr);
		throw XArchEvalWindows();
	}

	// start the service
	if (StartService(service, 0, NULL)) {
		LOG((CLOG_INFO "service '%s' started", m_daemonName.c_str()));
	}
	else {
		throw XArchEvalWindows();
	}
}

void
CMSWindowsApp::stopService()
{
	// open service manager
	SC_HANDLE mgr = OpenSCManager(NULL, NULL, GENERIC_READ);
	if (mgr == NULL) {
		throw XArchEvalWindows();
	}

	// open the service
	SC_HANDLE service = OpenService(
		mgr, m_daemonName.c_str(),
		SERVICE_STOP | SERVICE_QUERY_STATUS);

	if (service == NULL) {
		CloseServiceHandle(mgr);
		throw XArchEvalWindows();
	}

	// ask the service to stop, asynchronously
	SERVICE_STATUS ss;
	if (!ControlService(service, SERVICE_CONTROL_STOP, &ss)) {
		DWORD dwErrCode = GetLastError(); 
		if (dwErrCode != ERROR_SERVICE_NOT_ACTIVE) {
			LOG((CLOG_ERR "cannot stop service '%s'", m_daemonName.c_str()));
			throw XArchEvalWindows();
		}
	}

	LOG((CLOG_INFO "service '%s' stopping asyncronously", m_daemonName.c_str()));
}