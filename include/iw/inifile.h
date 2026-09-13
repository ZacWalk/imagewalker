#pragma once

// Portable settings: <exename>.ini beside the executable, in place of the
// registry keys the original releases used.
//
// The Win32 profile API is the whole implementation. It is not fast, but these
// apps read their settings once at startup and write them once at shutdown, and
// it gives us an on-disk format a user can read and edit.
//
// Nested registry keys become one flat section name with backslash separators,
// which is what SectionPath() is for:
//
//     [Settings\Toolbar]
//     Buttons=0a0b0c...

#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "iw/appinfo.h"

namespace IW
{
namespace Ini
{

namespace Detail
{

// GetPrivateProfileStringW only reads UTF-16 from a file that starts with a
// BOM, and WritePrivateProfileStringW only writes UTF-16 into one. Without
// this, settings containing non-ASCII characters are silently mangled through
// the ANSI code page.
inline void EnsureFile()
{
	static const bool done = []
	{
		LPCTSTR path = Paths::IniPath().c_str();

		if (::GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
			return true;

		const HANDLE file = ::CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
		                                 FILE_ATTRIBUTE_NORMAL, nullptr);

		if (file == INVALID_HANDLE_VALUE)
			return false;

#ifdef UNICODE
		const unsigned char bom[] = { 0xFF, 0xFE };
		DWORD written = 0;
		::WriteFile(file, bom, sizeof(bom), &written, nullptr);
#endif

		::CloseHandle(file);
		return true;
	}();

	(void) done;
}

inline int HexDigit(TCHAR c)
{
	if (c >= _T('0') && c <= _T('9'))
		return c - _T('0');

	if (c >= _T('a') && c <= _T('f'))
		return c - _T('a') + 10;

	if (c >= _T('A') && c <= _T('F'))
		return c - _T('A') + 10;

	return -1;
}

} // namespace Detail

// Joins a parent section with a child, matching the nesting the registry
// archives express with StartSection()/EndSection().
inline Paths::String SectionPath(const Paths::String &parent, const Paths::String &child)
{
	if (parent.empty())
		return child;

	if (child.empty())
		return parent;

	return parent + _T("\\") + child;
}

inline bool Exists(LPCTSTR section, LPCTSTR key)
{
	TCHAR value[4];
	// A missing key returns the default; a present but empty one returns "".
	return ::GetPrivateProfileString(section, key, _T("\x01"), value, ARRAYSIZE(value),
	                                 Paths::IniPath().c_str()) == 0 || value[0] != _T('\x01');
}

// True if the section has any keys of its own, or if any nested section sits
// below it. A registry key with no values but with subkeys opens successfully,
// and the property archives rely on that.
inline bool SectionExists(LPCTSTR section)
{
	const size_t length = _tcslen(section);

	if (length == 0)
		return false;

	std::vector<TCHAR> buffer(4096);

	for (;;)
	{
		const DWORD used = ::GetPrivateProfileSectionNames(&buffer[0],
		                                                   static_cast<DWORD>(buffer.size()),
		                                                   Paths::IniPath().c_str());

		if (used + 2 < buffer.size())
			break;

		if (buffer.size() > 1024 * 1024)
			return false;

		buffer.resize(buffer.size() * 2);
	}

	for (const TCHAR *name = &buffer[0]; *name != 0; name += _tcslen(name) + 1)
	{
		if (_tcsnicmp(name, section, length) != 0)
			continue;

		if (name[length] == 0 || name[length] == _T('\\'))
			return true;
	}

	return false;
}

inline bool ReadString(LPCTSTR section, LPCTSTR key, Paths::String &result)
{
	std::vector<TCHAR> buffer(512);

	for (;;)
	{
		const DWORD length = ::GetPrivateProfileString(section, key, _T(""), &buffer[0],
		                                               static_cast<DWORD>(buffer.size()),
		                                               Paths::IniPath().c_str());

		// The API signals truncation by filling the buffer bar one character.
		if (length + 1 < buffer.size())
		{
			result.assign(&buffer[0], length);
			return length != 0 || Exists(section, key);
		}

		if (buffer.size() > 1024 * 1024)
			return false;

		buffer.resize(buffer.size() * 2);
	}
}

inline Paths::String ReadString(LPCTSTR section, LPCTSTR key, LPCTSTR fallback)
{
	Paths::String result;
	return ReadString(section, key, result) ? result : Paths::String(fallback);
}

inline bool WriteString(LPCTSTR section, LPCTSTR key, LPCTSTR value)
{
	Detail::EnsureFile();
	return ::WritePrivateProfileString(section, key, value, Paths::IniPath().c_str()) != FALSE;
}

inline bool ReadDword(LPCTSTR section, LPCTSTR key, DWORD &value)
{
	Paths::String text;

	if (!ReadString(section, key, text) || text.empty())
		return false;

	value = static_cast<DWORD>(_tcstoul(text.c_str(), nullptr, 10));
	return true;
}

inline bool WriteDword(LPCTSTR section, LPCTSTR key, DWORD value)
{
	TCHAR text[24];
	_sntprintf_s(text, _TRUNCATE, _T("%lu"), value);
	return WriteString(section, key, text);
}

inline bool ReadInt(LPCTSTR section, LPCTSTR key, int &value)
{
	Paths::String text;

	if (!ReadString(section, key, text) || text.empty())
		return false;

	value = static_cast<int>(_tcstol(text.c_str(), nullptr, 10));
	return true;
}

inline bool WriteInt(LPCTSTR section, LPCTSTR key, int value)
{
	TCHAR text[24];
	_sntprintf_s(text, _TRUNCATE, _T("%d"), value);
	return WriteString(section, key, text);
}

inline bool ReadBool(LPCTSTR section, LPCTSTR key, bool &value)
{
	int number = 0;

	if (!ReadInt(section, key, number))
		return false;

	value = number != 0;
	return true;
}

inline bool WriteBool(LPCTSTR section, LPCTSTR key, bool value)
{
	return WriteString(section, key, value ? _T("1") : _T("0"));
}

// Blobs -- window placements, toolbar layouts, rebar band state -- are stored
// as lower case hex so the file stays a text file.
inline bool ReadBinary(LPCTSTR section, LPCTSTR key, std::vector<BYTE> &value)
{
	Paths::String text;

	if (!ReadString(section, key, text) || text.empty() || (text.size() & 1) != 0)
		return false;

	value.clear();
	value.reserve(text.size() / 2);

	for (size_t i = 0; i < text.size(); i += 2)
	{
		const int high = Detail::HexDigit(text[i]);
		const int low = Detail::HexDigit(text[i + 1]);

		if (high < 0 || low < 0)
		{
			value.clear();
			return false;
		}

		value.push_back(static_cast<BYTE>((high << 4) | low));
	}

	return true;
}

inline bool WriteBinary(LPCTSTR section, LPCTSTR key, const void *data, size_t size)
{
	static const TCHAR digits[] = _T("0123456789abcdef");

	const BYTE *bytes = static_cast<const BYTE *>(data);
	Paths::String text;
	text.reserve(size * 2);

	for (size_t i = 0; i < size; ++i)
	{
		text += digits[bytes[i] >> 4];
		text += digits[bytes[i] & 0x0F];
	}

	return WriteString(section, key, text.c_str());
}

// RegQueryValueEx-shaped: the caller passes the buffer size and gets the stored
// size back. Records are genuinely variable length (PIDLs), so a short one is
// not an error here -- a caller reading into a fixed struct must zero it first
// and then check the returned size.
inline bool ReadBinary(LPCTSTR section, LPCTSTR key, void *data, DWORD &size)
{
	std::vector<BYTE> value;

	if (!ReadBinary(section, key, value) || value.size() > size)
		return false;

	if (!value.empty())
		memcpy(data, &value[0], value.size());

	size = static_cast<DWORD>(value.size());
	return true;
}

inline bool DeleteKey(LPCTSTR section, LPCTSTR key)
{
	return ::WritePrivateProfileString(section, key, nullptr, Paths::IniPath().c_str()) != FALSE;
}

inline bool DeleteSection(LPCTSTR section)
{
	return ::WritePrivateProfileString(section, nullptr, nullptr, Paths::IniPath().c_str()) != FALSE;
}

// The profile API caches writes; call this before the process exits.
inline void Flush()
{
	::WritePrivateProfileString(nullptr, nullptr, nullptr, Paths::IniPath().c_str());
}

} // namespace Ini
} // namespace IW
