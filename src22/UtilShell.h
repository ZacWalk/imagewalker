// ImageWalker by Zac Walker
//
// Purpose: Shell wrappers: PIDL items, IShellFolder, enumeration and display
//          names.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

//
// The shell item / folder / enumerator wrappers moved to include/iw/shell.h
// and are shared by every WTL app. Only the exception flavour is per app: 2.3
// throws the std types, 2.2 and 2.0 throw hierarchies with no std::exception
// base, so this cannot live in the shared header.
//
///////////////////////////////////////////////////////////////////////

#ifndef _WINDOWS_SHELL_H_
#define _WINDOWS_SHELL_H_

#include <WinInet.h>
#include <ShlObj.h>
#include <ShlwApi.h> // For PathIsUrl function

#include "UtilBase.h"
#include "iw/shell.h"

namespace IW
{
	inline void ShellApiFailed(LPCTSTR szApi, HRESULT hr)
	{
		ATLTRACE(_T("%s failed, hr=%lx\n"), szApi, hr);
		throw std::exception("Shell API call failed");
	}

	inline void ShellOutOfMemory()
	{
		throw std::bad_alloc();
	}
}

#endif //_WINDOWS_SHELL_H_
