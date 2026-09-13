#include "Platform.h"
#include "PlatformWin32.h"

#include <htmlhelp.h>

namespace iw::platform
{
	bool show_help(const WindowFramePtr& owner)
	{
		const auto path = module_folder() / L"ImageWalker.chm";
		const auto topic = path.wstring() + L"::/iw30.html";
		if (HtmlHelpW(win32::owner_window(owner), topic.c_str(), HH_DISPLAY_TOPIC, 0)) return true;
		show_error(L"The ImageWalker 3.0 help could not be opened. Keep ImageWalker.chm beside the executable.",
			L"ImageWalker help", owner);
		return false;
	}
}
