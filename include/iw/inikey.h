#pragma once

// A CRegKey-shaped facade over <exename>.ini.
//
// ImageWalker 2.00 talks to the registry directly from a dozen dialogs and
// wizards rather than through a property archive. This keeps those call sites
// intact -- Open/Create/QueryValue/SetValue/Close -- while the values land in
// the portable ini file instead of HKEY_CURRENT_USER.
//
// A "key" here is just a section name. Nested keys are flattened with
// backslashes, exactly as the registry path would read.

#include <windows.h>
#include <tchar.h>

#include "iw/inifile.h"

namespace IW
{

class CIniKey
{
public:

	CIniKey()
	{
	}

	// Root of the tree. The hive is ignored: settings are per executable now.
	LONG Open(HKEY, LPCTSTR lpszName)
	{
		return Assign(Paths::String(lpszName), false);
	}

	LONG Create(HKEY, LPCTSTR lpszName)
	{
		return Assign(Paths::String(lpszName), true);
	}

	LONG Open(const CIniKey &parent, LPCTSTR lpszName)
	{
		return Assign(Ini::SectionPath(parent._section, lpszName), false);
	}

	LONG Create(const CIniKey &parent, LPCTSTR lpszName)
	{
		return Assign(Ini::SectionPath(parent._section, lpszName), true);
	}

	LONG Close()
	{
		_section.clear();
		return ERROR_SUCCESS;
	}

	bool IsOpen() const { return !_section.empty(); }
	LPCTSTR Section() const { return _section.c_str(); }

	LONG QueryValue(DWORD &dwValue, LPCTSTR lpszValueName) const
	{
		return Ini::ReadDword(Section(), lpszValueName, dwValue)
			       ? ERROR_SUCCESS
			       : ERROR_FILE_NOT_FOUND;
	}

	// pdwCount is the buffer size in characters going in, and the length of the
	// string coming out.
	LONG QueryValue(LPTSTR szValue, LPCTSTR lpszValueName, DWORD *pdwCount) const
	{
		if (pdwCount == nullptr)
			return ERROR_INVALID_PARAMETER;

		Paths::String value;

		if (!Ini::ReadString(Section(), lpszValueName, value))
			return ERROR_FILE_NOT_FOUND;

		const DWORD needed = static_cast<DWORD>(value.size()) + 1;

		// CRegKey reports the size it needs, which is what the two-pass
		// "query size, allocate, query again" idiom depends on.
		if (szValue == nullptr || needed > *pdwCount)
		{
			*pdwCount = needed;
			return szValue == nullptr ? ERROR_SUCCESS : ERROR_MORE_DATA;
		}

		_tcscpy_s(szValue, *pdwCount, value.c_str());
		*pdwCount = static_cast<DWORD>(value.size());

		return ERROR_SUCCESS;
	}

	LONG QueryBinary(LPCTSTR lpszValueName, void *pData, DWORD &dwSize) const
	{
		return Ini::ReadBinary(Section(), lpszValueName, pData, dwSize)
			       ? ERROR_SUCCESS
			       : ERROR_FILE_NOT_FOUND;
	}

	LONG SetValue(DWORD dwValue, LPCTSTR lpszValueName)
	{
		return Ini::WriteDword(Section(), lpszValueName, dwValue)
			       ? ERROR_SUCCESS
			       : ERROR_ACCESS_DENIED;
	}

	LONG SetValue(LPCTSTR lpszValue, LPCTSTR lpszValueName)
	{
		return Ini::WriteString(Section(), lpszValueName, lpszValue)
			       ? ERROR_SUCCESS
			       : ERROR_ACCESS_DENIED;
	}

	LONG SetBinary(LPCTSTR lpszValueName, const void *pData, DWORD dwSize)
	{
		return Ini::WriteBinary(Section(), lpszValueName, pData, dwSize)
			       ? ERROR_SUCCESS
			       : ERROR_ACCESS_DENIED;
	}

	LONG DeleteValue(LPCTSTR lpszValueName)
	{
		return Ini::DeleteKey(Section(), lpszValueName) ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
	}

private:

	// Opening is a query: it fails when nothing was ever stored, which is what
	// the callers use to decide whether to keep their hard-coded defaults.
	LONG Assign(const Paths::String &section, bool create)
	{
		if (!create && !Ini::SectionExists(section.c_str()))
		{
			_section.clear();
			return ERROR_FILE_NOT_FOUND;
		}

		_section = section;
		return ERROR_SUCCESS;
	}

	Paths::String _section;
};

} // namespace IW
