#pragma once

// Paths derived from the running executable, shared by the four WTL apps.
// These builds are portable: settings and the log sit beside the exe and
// are named after it, so imagewalker23d.exe uses imagewalker23d.ini/.log and
// never collides with a release build in the same directory.
//
// Header-only and TCHAR-based, so it compiles correctly into MBCS ArtMate 1.0
// and Unicode ImageWalker 2.0, 2.2 and 2.3 alike.

#include <windows.h>
#include <tchar.h>
#include <string>

namespace IW
{
namespace Paths
{

typedef std::basic_string<TCHAR> String;

inline const String &ModulePath()
{
	static const String path = []
	{
		// The manifest declares longPathAware, so MAX_PATH is not the limit.
		TCHAR buffer[4096] = {};
		const DWORD length = ::GetModuleFileName(nullptr, buffer, ARRAYSIZE(buffer));
		// Truncation returns the buffer size; a partial path would silently
		// name the wrong ini and log.
		return length > 0 && length < ARRAYSIZE(buffer) ? String(buffer, length) : String();
	}();

	return path;
}

// Includes the trailing backslash.
inline const String &ModuleDir()
{
	static const String dir = []
	{
		const String &path = ModulePath();
		const size_t slash = path.find_last_of(_T("\\/"));
		return slash == String::npos ? String() : path.substr(0, slash + 1);
	}();

	return dir;
}

// File name with no directory and no extension, e.g. "imagewalker231d".
inline const String &ModuleStem()
{
	static const String stem = []
	{
		String name = ModulePath().substr(ModuleDir().size());
		const size_t dot = name.find_last_of(_T('.'));

		if (dot != String::npos)
			name.erase(dot);

		return name;
	}();

	return stem;
}

inline String SiblingPath(LPCTSTR suffix)
{
	return ModuleDir() + ModuleStem() + suffix;
}

inline const String &IniPath()
{
	static const String path = SiblingPath(_T(".ini"));
	return path;
}

inline const String &LogPath()
{
	static const String path = SiblingPath(_T(".log"));
	return path;
}

} // namespace Paths
} // namespace IW
