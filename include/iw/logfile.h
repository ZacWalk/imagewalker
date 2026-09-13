#pragma once

// A plain text run log written to <exename>.log beside the executable.
//
// Open() truncates, so the file always describes the most recent run. Counters
// accumulate over the run and are dumped by Close(), which gives a cheap way to
// see how much work an app actually did without attaching a debugger.
//
// Header-only and TCHAR-based; the file itself is always UTF-8 so the two
// character sets in this repository produce comparable output.
//
// Never call any of this from the crash reporter: Detail::Format takes a
// non-recursive mutex, so a thread that faults while holding it would deadlock
// the reporter. crashreport.cpp deliberately uses raw WriteFile instead.

#include <windows.h>
#include <tchar.h>
#include <stdarg.h>
#include <map>
#include <mutex>
#include <string>

#include "iw/appinfo.h"

namespace IW
{
namespace Logging
{

namespace Detail
{

struct State
{
	std::mutex mutex;
	HANDLE file = INVALID_HANDLE_VALUE;
	// Unsigned subtraction keeps elapsed ticks correct across a DWORD wrap.
	DWORD startTick = 0;
	std::map<Paths::String, long long> counters;
	long warnings = 0;
	long errors = 0;
};

inline State &Get()
{
	// Leaked on purpose. A destructor registered with atexit would let a thread
	// that outlives main() lock a destroyed mutex, and destroying a mutex that
	// somebody is blocked on is undefined anyway.
	static State *const state = new State();
	return *state;
}

inline std::string ToUtf8(LPCTSTR text, int length)
{
#ifdef UNICODE
	const wchar_t *wide = text;
	const int wideLength = length;
#else
	std::wstring owned;
	const int needed = ::MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
	owned.resize(needed > 0 ? needed : 0);

	if (needed > 0)
		::MultiByteToWideChar(CP_ACP, 0, text, length, &owned[0], needed);

	const wchar_t *wide = owned.c_str();
	const int wideLength = needed > 0 ? needed : 0;
#endif

	if (wideLength <= 0)
		return std::string();

	const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, wideLength, nullptr, 0, nullptr, nullptr);
	std::string utf8;
	utf8.resize(bytes > 0 ? bytes : 0);

	if (bytes > 0)
		::WideCharToMultiByte(CP_UTF8, 0, wide, wideLength, &utf8[0], bytes, nullptr, nullptr);

	return utf8;
}

// Caller holds the lock.
inline void Emit(State &state, LPCTSTR level, LPCTSTR text)
{
	if (state.file == INVALID_HANDLE_VALUE)
		return;

	SYSTEMTIME now;
	::GetLocalTime(&now);

	TCHAR line[2048];
	int length = _sntprintf_s(line, _TRUNCATE, _T("%02u:%02u:%02u.%03u %-5s %s\r\n"),
	                          now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, level, text);

	// _TRUNCATE reports -1 when it clipped, but the buffer is still terminated.
	if (length < 0)
		length = static_cast<int>(_tcslen(line));

	if (length <= 0)
		return;

	const std::string utf8 = ToUtf8(line, length);
	DWORD written = 0;
	::WriteFile(state.file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

inline void Format(LPCTSTR level, LPCTSTR format, va_list args)
{
	TCHAR text[2048];

	if (_vsntprintf_s(text, _TRUNCATE, format, args) < 0)
		text[ARRAYSIZE(text) - 1] = 0;

	State &state = Get();
	std::lock_guard<std::mutex> lock(state.mutex);
	Emit(state, level, text);
}

} // namespace Detail

// Creates (truncating) <exename>.log and writes the banner. Safe to call when
// the directory is read-only: logging then silently does nothing.
inline void Open(LPCTSTR appName)
{
	Detail::State &state = Detail::Get();
	std::lock_guard<std::mutex> lock(state.mutex);

	if (state.file != INVALID_HANDLE_VALUE)
		return;

	// Sharing writes is not an option: CREATE_ALWAYS truncates, so a second
	// writer would interleave. A second instance gets its own pid-named log
	// instead of silently getting none.
	const DWORD share = FILE_SHARE_READ | FILE_SHARE_DELETE;

	state.file = ::CreateFile(Paths::LogPath().c_str(), GENERIC_WRITE, share, nullptr,
	                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (state.file == INVALID_HANDLE_VALUE)
	{
		TCHAR suffix[32];
		_sntprintf_s(suffix, _TRUNCATE, _T("-%lu.log"), ::GetCurrentProcessId());
		state.file = ::CreateFile(Paths::SiblingPath(suffix).c_str(), GENERIC_WRITE, share, nullptr,
		                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	}

	if (state.file == INVALID_HANDLE_VALUE)
		return;

	state.startTick = ::GetTickCount();

	TCHAR banner[1024];
	_sntprintf_s(banner, _TRUNCATE, _T("%s starting -- %s"), appName, Paths::ModulePath().c_str());
	Detail::Emit(state, _T("info"), banner);

	_sntprintf_s(banner, _TRUNCATE, _T("settings: %s"), Paths::IniPath().c_str());
	Detail::Emit(state, _T("info"), banner);

	_sntprintf_s(banner, _TRUNCATE, _T("command line: %s"), ::GetCommandLine());
	Detail::Emit(state, _T("info"), banner);
}

inline void Write(LPCTSTR format, ...)
{
	va_list args;
	va_start(args, format);
	Detail::Format(_T("info"), format, args);
	va_end(args);
}

inline void Warn(LPCTSTR format, ...)
{
	{
		// Its own scope: Detail::Format takes the same non-recursive mutex.
		Detail::State &state = Detail::Get();
		std::lock_guard<std::mutex> lock(state.mutex);
		++state.warnings;
	}

	va_list args;
	va_start(args, format);
	Detail::Format(_T("warn"), format, args);
	va_end(args);
}

inline void Error(LPCTSTR format, ...)
{
	{
		Detail::State &state = Detail::Get();
		std::lock_guard<std::mutex> lock(state.mutex);
		++state.errors;
	}

	va_list args;
	va_start(args, format);
	Detail::Format(_T("error"), format, args);
	va_end(args);
}

// Logs the last Win32 error against a named operation.
inline void LastError(LPCTSTR what)
{
	const DWORD code = ::GetLastError();
	LPTSTR message = nullptr;

	::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	                nullptr, code, 0, reinterpret_cast<LPTSTR>(&message), 0, nullptr);

	Error(_T("%s failed: %lu %s"), what, code, message ? message : _T(""));

	if (message)
		::LocalFree(message);
}

// Diagnostic counter, dumped by Close(). Cheap enough to call per file.
inline void Counter(LPCTSTR name, long long delta = 1)
{
	Detail::State &state = Detail::Get();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.counters[name] += delta;
}

inline void Close()
{
	Detail::State &state = Detail::Get();
	std::lock_guard<std::mutex> lock(state.mutex);

	if (state.file == INVALID_HANDLE_VALUE)
		return;

	TCHAR line[1024];

	for (std::map<Paths::String, long long>::const_iterator it = state.counters.begin();
	     it != state.counters.end(); ++it)
	{
		_sntprintf_s(line, _TRUNCATE, _T("counter %-32s %lld"), it->first.c_str(), it->second);
		Detail::Emit(state, _T("info"), line);
	}

	_sntprintf_s(line, _TRUNCATE, _T("uptime %lu ms, %ld warnings, %ld errors"),
	             ::GetTickCount() - state.startTick, state.warnings, state.errors);
	Detail::Emit(state, _T("info"), line);
	Detail::Emit(state, _T("info"), _T("exiting"));

	::CloseHandle(state.file);
	state.file = INVALID_HANDLE_VALUE;
}

} // namespace Logging
} // namespace IW
