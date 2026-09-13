#pragma once

// Opens ImageWalker.chm, which sits beside the executable.
//
// All four WTL apps ship the same help file, so they have to find it the same
// way. These builds are portable - the exe may be run from anywhere - so the
// path is derived from the module rather than from the working directory or
// from an install location in the registry.
//
// Header-only and TCHAR-based, so it compiles into MBCS ArtMate 1.0 and
// Unicode ImageWalker 2.0, 2.2 and 2.3 alike.

#include <windows.h>
#include <tchar.h>
#include <htmlhelp.h>

#include "iw/appinfo.h"
#include "iw/helpids.h"

namespace IW
{

inline const Paths::String &HelpFilePath()
{
	static const Paths::String path = Paths::ModuleDir() + _T("ImageWalker.chm");
	return path;
}

// nId of 0 opens the default topic.
inline void InvokeHelp(HWND hwnd, UINT nId)
{
	const Paths::String &path = HelpFilePath();

	if (nId == 0)
		::HtmlHelp(hwnd, path.c_str(), HH_DISPLAY_TOPIC, 0);
	else
		::HtmlHelp(hwnd, path.c_str(), HH_HELP_CONTEXT, nId);
}

} // namespace IW
