#pragma once

// Settings go to <exename>.ini beside the executable rather than the registry,
// so the app is portable: copy the folder and the settings travel with it.

#include "iw/appinfo.h"

namespace Settings
{
	inline LPCTSTR File()
	{
		static CString strPath;

		if (strPath.IsEmpty())
			strPath = IW::Paths::IniPath().c_str();

		return strPath;
	}

	inline CString GetString(LPCTSTR szSection, LPCTSTR szKey, LPCTSTR szDefault = _T(""))
	{
		TCHAR szValue[1024] = {0};
		::GetPrivateProfileString(szSection, szKey, szDefault, szValue, _countof(szValue), File());
		return CString(szValue);
	}

	inline void SetString(LPCTSTR szSection, LPCTSTR szKey, LPCTSTR szValue)
	{
		::WritePrivateProfileString(szSection, szKey, szValue, File());
	}

	// GetPrivateProfileInt stops at the sign, so a negative window coordinate
	// would not round-trip; parse the string instead.
	inline int GetInt(LPCTSTR szSection, LPCTSTR szKey, int nDefault)
	{
		const CString str = GetString(szSection, szKey);
		return str.IsEmpty() ? nDefault : _ttoi(str);
	}

	inline void SetInt(LPCTSTR szSection, LPCTSTR szKey, int nValue)
	{
		CString str;
		str.Format(_T("%d"), nValue);
		SetString(szSection, szKey, str);
	}

	// PIDLs have no text form, so they go in as hex.
	inline bool GetBinary(LPCTSTR szSection, LPCTSTR szKey, std::vector<BYTE>& data)
	{
		auto HexDigit = [](TCHAR ch) -> int
		{
			if (ch >= _T('0') && ch <= _T('9')) return ch - _T('0');
			if (ch >= _T('a') && ch <= _T('f')) return ch - _T('a') + 10;
			if (ch >= _T('A') && ch <= _T('F')) return ch - _T('A') + 10;
			return -1;
		};

		data.clear();

		const CString str = GetString(szSection, szKey);
		const int nLen = str.GetLength();

		if (nLen == 0 || (nLen & 1))
			return false;

		data.resize(nLen / 2);

		for (int i = 0; i < nLen; i += 2)
		{
			const int hi = HexDigit(str[i]);
			const int lo = HexDigit(str[i + 1]);

			if (hi < 0 || lo < 0)
			{
				data.clear();
				return false;
			}

			data[i / 2] = static_cast<BYTE>((hi << 4) | lo);
		}

		return true;
	}

	inline void SetBinary(LPCTSTR szSection, LPCTSTR szKey, const BYTE* pData, UINT nBytes)
	{
		static constexpr TCHAR szHex[] = _T("0123456789ABCDEF");

		CString str;
		LPTSTR psz = str.GetBufferSetLength(static_cast<int>(nBytes) * 2);

		for (UINT i = 0; i < nBytes; i++)
		{
			psz[i * 2] = szHex[pData[i] >> 4];
			psz[i * 2 + 1] = szHex[pData[i] & 0x0F];
		}

		str.ReleaseBuffer();

		SetString(szSection, szKey, str);
	}

	// Window placement is the only layout state worth persisting as a group.
	inline void SaveWindowPlacement(HWND hWnd, LPCTSTR szSection = _T("Window"))
	{
		WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};

		if (!::GetWindowPlacement(hWnd, &wp))
			return;

		// Named values, not a struct blob: the layout of WINDOWPLACEMENT is not
		// something to bake into a settings file.
		SetInt(szSection, _T("Left"), wp.rcNormalPosition.left);
		SetInt(szSection, _T("Top"), wp.rcNormalPosition.top);
		SetInt(szSection, _T("Right"), wp.rcNormalPosition.right);
		SetInt(szSection, _T("Bottom"), wp.rcNormalPosition.bottom);
		SetInt(szSection, _T("Show"), wp.showCmd);
	}

	inline bool RestoreWindowPlacement(HWND hWnd, LPCTSTR szSection = _T("Window"))
	{
		WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};

		wp.rcNormalPosition.left = GetInt(szSection, _T("Left"), -1);
		wp.rcNormalPosition.top = GetInt(szSection, _T("Top"), -1);
		wp.rcNormalPosition.right = GetInt(szSection, _T("Right"), -1);
		wp.rcNormalPosition.bottom = GetInt(szSection, _T("Bottom"), -1);
		wp.showCmd = GetInt(szSection, _T("Show"), SW_SHOWNORMAL);

		if (wp.rcNormalPosition.right <= wp.rcNormalPosition.left ||
			wp.rcNormalPosition.bottom <= wp.rcNormalPosition.top)
			return false;

		// A saved position can land off-screen if the monitor layout changed.
		if (::MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONULL) == nullptr)
			return false;

		if (wp.showCmd == SW_SHOWMINIMIZED)
			wp.showCmd = SW_SHOWNORMAL;

		return ::SetWindowPlacement(hWnd, &wp) != FALSE;
	}
}
