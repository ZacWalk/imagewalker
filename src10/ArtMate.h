// ArtMate.h : the application-wide bits.

#pragma once

#include "resource.h"
#include "Settings.h"

// The folder named on the command line, if any.
extern CString g_strCmdLine;

// Popup positions in IDR_MAINFRAME. The two gutter strips drop the frame's own
// menus rather than building their own copies of them.
enum { kMenuFile, kMenuEdit, kMenuImage, kMenuView, kMenuHelp };

// A file size for display: whole KB below a megabyte, one decimal above.
inline CString FormatFileSize(LONGLONG nBytes)
{
	CString str;

	if (nBytes >= 1024LL * 1024 * 1024)
		str.Format(_T("%.1f GB"), static_cast<double>(nBytes) / (1024.0 * 1024.0 * 1024.0));
	else if (nBytes >= 1024 * 1024)
		str.Format(_T("%.1f MB"), static_cast<double>(nBytes) / (1024.0 * 1024.0));
	else
		str.Format(_T("%I64d KB"), (nBytes + 1023) / 1024);

	return str;
}
