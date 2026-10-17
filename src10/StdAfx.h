// stdafx.h : the headers that everything in ArtMate uses.

#pragma once

// STL first. Under /permissive, pulling these in after the app headers breaks
// std::plus<void> inside xutility.
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <memory>

// <mutex> belongs in this block for the same reason as the rest. iw/workqueue.h
// pulls it in, and reaching it only after the app headers makes the debug
// build's front end fall over with an internal compiler error inside
// xcall_once.h. 2.21 hit exactly this.
#include <mutex>

#include <stdio.h>
#include <tchar.h>

#include <atlbase.h>
#include <atlstr.h>

// The Vista menu path in atlctrlw.h owner-draws through a bitmap it builds per
// item; the other four apps all turn it off, and the command bar is custom-drawn
// here as well.
#define _WTL_CMDBAR_VISTA_MENUS 0

#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
#include <atlsplit.h>
#include <atlctrls.h>
#include <atlctrlw.h>
#include <atlctrlx.h>
#include <atldlgs.h>
#include <atlgdi.h>
#include <atluser.h>
#include <atlmisc.h>
#include <atlcrack.h>

// atlapp.h drags in atlres.h, which claims ID_EDIT_DELETE and ID_VIEW_REFRESH
// for the standard WTL command ids. ArtMate's resource.h assigns its own values
// to both, and its menus and accelerators are built against those.
#undef ID_EDIT_DELETE
#undef ID_VIEW_REFRESH

#include <wininet.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>

// The diagnostics the 1998 sources use, over the ones the CRT and ATL supply.
#ifndef ASSERT
#define ASSERT(expr) ATLASSERT(expr)
#endif
#ifndef VERIFY
#ifdef _DEBUG
#define VERIFY(expr) ATLASSERT(expr)
#else
#define VERIFY(expr) ((void)(expr))
#endif
#endif
#ifndef TRACE
#define TRACE ATLTRACE
#endif
#ifndef ASSERT_VALID
#define ASSERT_VALID(p) ATLASSERT((p) != NULL)
#endif

// MFC's leak-tracking allocator; the CRT debug heap covers this now.
#define DEBUG_NEW new

// The shell headers no longer define this flag.
#ifndef SHGDN_INCLUDE_NONFILESYS
#define SHGDN_INCLUDE_NONFILESYS 0x2000
#endif

#include "Error.h"
#include "Settings.h"

#include "shell.h"
#include "Dib.h"
#include "Status.h"
