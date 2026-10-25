#include "Platform.h"
#include "PlatformWin32.h"
#include "help_file.h"

#include <htmlhelp.h>

namespace iw::platform
{
	bool show_help(const WindowFramePtr& owner)
	{
		// The embedded help is opened from an unmarked copy; see help_file.h.
		const auto path = iw::help::prepare_embedded();
		if (path.empty())
		{
			show_error(L"The ImageWalker 3.0 help could not be written to the temporary folder.",
				L"ImageWalker help", owner);
			return false;
		}
		const auto topic = path + L"::/iw30.html";
		if (HtmlHelpW(win32::owner_window(owner), topic.c_str(), HH_DISPLAY_TOPIC, 0)) return true;
		show_error(L"The ImageWalker 3.0 help could not be opened.", L"ImageWalker help", owner);
		return false;
	}
}
