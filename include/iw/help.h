#pragma once

// Opens the compiled help the executable embeds. help_file.h explains why it
// is written to the temporary folder rather than read from beside the exe: a
// help file extracted from a downloaded zip, or on a network share, shows only
// blank pages.
//
// Header-only and wide-character, so it compiles into MBCS ArtMate 1.0 and
// Unicode ImageWalker 2.0, 2.2 and 2.3 alike.

#include <windows.h>
#include <htmlhelp.h>

#include "help_file.h"
#include "iw/helpids.h"

namespace IW
{

// nId of 0 opens the default topic.
inline void InvokeHelp(HWND hwnd, UINT nId)
{
	const std::wstring path = iw::help::prepare_embedded();

	if (path.empty())
	{
		::MessageBoxW(hwnd, L"The help file could not be written to the temporary folder.", L"Help",
		              MB_OK | MB_ICONWARNING);
		return;
	}

	if (nId == 0)
		::HtmlHelpW(hwnd, path.c_str(), HH_DISPLAY_TOPIC, 0);
	else
		::HtmlHelpW(hwnd, path.c_str(), HH_HELP_CONTEXT, nId);
}

} // namespace IW
