#pragma once

#include <windows.h>
#include <commdlg.h>

namespace IW
{
	inline void SetPrintDialogPageRange(PRINTDLG &dialog, int pageCount)
	{
		// PRINTDLG's selectable range is 16-bit; "All" still prints every page.
		const int limit = pageCount < 1 ? 1 : pageCount > 0xffff ? 0xffff : pageCount;
		dialog.nMinPage = dialog.nFromPage = 1;
		dialog.nMaxPage = dialog.nToPage = static_cast<WORD>(limit);
	}

	inline bool GetPrintJobPageRange(const PRINTDLG &dialog, int pageCount,
		unsigned long &first, unsigned long &last)
	{
		first = (dialog.Flags & PD_PAGENUMS) ? dialog.nFromPage : 1;
		last = (dialog.Flags & PD_PAGENUMS) ? dialog.nToPage : static_cast<unsigned long>(pageCount);
		return pageCount > 0 && first >= 1 && first <= last && last <= static_cast<unsigned long>(pageCount);
	}
}
