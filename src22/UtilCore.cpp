// ImageWalker by Zac Walker
//
// Purpose: Core helper implementation - allocation, path handling and the
//          file operation wrappers.
//
// Copyright (C) 1998-2026 Zac Walker. MIT licence - see LICENSE.
// For more information on ImageWalker see www.ImageWalker.com

// UtilCore.cpp: the shared utility implementations.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"


IW::CMessageBoxIndirect::CMessageBoxIndirect()
{
	cbSize = sizeof(MSGBOXPARAMS);
	hwndOwner = GetMainWindow();
	hInstance = nullptr;
	lpszCaption = g_szImageWalker;
	lpszIcon = nullptr;
	dwContextHelpId = HELP_CONTACT_SUPPORT;
	lpfnMsgBoxCallback = ErrorBoxCallBack;
	dwLanguageId = App.GetLangId();
	lpszText = _T("?");
	dwStyle = MB_ICONHAND | MB_OK | MB_HELP;
}

VOID CALLBACK IW::CMessageBoxIndirect::ErrorBoxCallBack(LPHELPINFO lpHelpInfo)
{
	App.InvokeHelp(GetMainWindow(), HELP_CONTACT_SUPPORT);
}

int IW::CMessageBoxIndirect::Show(const CString& str, unsigned nStyle)
{
	lpszText = str;
	dwStyle = nStyle;
	return MessageBoxIndirect(this);
}

int IW::CMessageBoxIndirect::Show(unsigned nId, unsigned nStyle)
{
	CString str;
	str.LoadString(nId);
	return Show(str, nStyle);
}

int IW::CMessageBoxIndirect::Show(unsigned nIdFormat, const CString& strMessage, unsigned nStyle)
{
	CString str;
	str.Format(nIdFormat, static_cast<LPCTSTR>(strMessage));
	return Show(str, nStyle);
}

int IW::CMessageBoxIndirect::ShowException(unsigned nId, _com_error& e, unsigned nStyle)
{
	return Show(nId, e.ErrorMessage(), nStyle);
}


int IW::CMessageBoxIndirect::ShowException(unsigned nId, std::exception& e, unsigned nStyle)
{
	return Show(nId, CString(e.what()), nStyle);
}


int IW::CMessageBoxIndirect::ShowOsError(unsigned nId, int nError, unsigned nStyle)
{
	LPVOID lpMsgBuf = nullptr;

	if (nError != 0)
	{
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			nError,
			App.GetLangId(), // Default language
			(LPTSTR)&lpMsgBuf,
			0,
			nullptr
		);
	}

	// Record the error.
	CString str;
	str.Format(nId, lpMsgBuf ? static_cast<LPCTSTR>(lpMsgBuf) : App.LoadString(IDS_NONE));


	// Free the buffer.
	if (lpMsgBuf)
		LocalFree(lpMsgBuf);

	return Show(str, nStyle);
}

int IW::CMessageBoxIndirect::ShowOsErrorWithFile(const CString& strFile, unsigned nId, int nError, unsigned nStyle)
{
	LPVOID lpMsgBuf = nullptr;

	if (nError != 0)
	{
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			nError,
			App.GetLangId(), // Default language
			(LPTSTR)&lpMsgBuf,
			0,
			nullptr
		);
	}

	// Record the error.
	CString str1, str2;

	str1.Format(nId, static_cast<LPCTSTR>(strFile));
	str2.Format(IDS_FILE_ERROR, lpMsgBuf ? static_cast<LPCTSTR>(lpMsgBuf) : App.LoadString(IDS_NONE));

	CString str;
	str = str1;
	str += _T("\n");
	str += str2;


	// Free the buffer.
	if (lpMsgBuf)
		LocalFree(lpMsgBuf);

	return Show(str, nStyle);
}


class WindowsVersion
{
public:
	OSVERSIONINFO m_info;

	WindowsVersion()
	{
		IW::MemZero(&m_info, sizeof(OSVERSIONINFO));
		m_info.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx(&m_info);
	}

	~WindowsVersion()
	{
	}

	bool IsWindows95(void)
	{
		if (m_info.dwPlatformId != VER_PLATFORM_WIN32_WINDOWS)
		{
			return false;
		}
		return m_info.dwMinorVersion == 0;
	}

	bool IsWindows98(void)
	{
		if (m_info.dwPlatformId != VER_PLATFORM_WIN32_WINDOWS)
		{
			return false;
		}
		return m_info.dwMinorVersion == 10;
	}

	bool IsWindowsME(void)
	{
		if (m_info.dwPlatformId != VER_PLATFORM_WIN32_WINDOWS)
		{
			return false;
		}
		return m_info.dwMinorVersion == 90;
	}

	bool IsWindowsXP(void)
	{
		if (m_info.dwPlatformId != VER_PLATFORM_WIN32_NT)
		{
			return false;
		}
		return m_info.dwMajorVersion == 5 && m_info.dwMinorVersion >= 1;
	}

	bool IsWindows2000(void)
	{
		if (m_info.dwPlatformId != VER_PLATFORM_WIN32_NT)
		{
			return false;
		}
		return m_info.dwMajorVersion == 5 && m_info.dwMinorVersion == 0;
	}

	bool IsWindowsNT4(void)
	{
		if (m_info.dwPlatformId != VER_PLATFORM_WIN32_NT)
		{
			return false;
		}
		return m_info.dwMajorVersion == 4;
	}

	bool IsWindowsVista(void)
	{
		if (m_info.dwPlatformId != VER_PLATFORM_WIN32_NT)
		{
			return false;
		}
		return m_info.dwMajorVersion == 6;
	}
};

WindowsVersion* GetWindowsVersion()
{
	static auto pVer = new WindowsVersion();
	return pVer;
}

bool IW::IsWindowsAscii()
{
	return IsWindows95() ||
		IsWindows98() ||
		GetWindowsVersion()->IsWindowsME();
}

// Windows Version
bool IW::IsWindows95()
{
	return GetWindowsVersion()->IsWindows95();
}

bool IW::IsWindows98()
{
	return GetWindowsVersion()->IsWindows98();
}

bool IW::IsWindowsME()
{
	return GetWindowsVersion()->IsWindowsME();
}

bool IW::IsWindowsXP()
{
	return GetWindowsVersion()->IsWindowsXP();
}

bool IW::IsWindowsVista()
{
	return GetWindowsVersion()->IsWindowsVista();
}

bool IW::IsWindowsXPOrBetter()
{
	if (GetWindowsVersion()->m_info.dwPlatformId != VER_PLATFORM_WIN32_NT)
	{
		return false;
	}

	if (GetWindowsVersion()->m_info.dwMajorVersion > 5)
	{
		return true;
	}

	return GetWindowsVersion()->m_info.dwMajorVersion == 5 && GetWindowsVersion()->m_info.dwMinorVersion >= 1;
}

bool IW::IsWindows2000()
{
	return GetWindowsVersion()->IsWindows2000();
}

bool IW::IsWindowsNT4()
{
	return GetWindowsVersion()->IsWindowsNT4();
}

bool IW::IsWindowsNT()
{
	return VER_PLATFORM_WIN32_NT == GetWindowsVersion()->m_info.dwPlatformId;
}
