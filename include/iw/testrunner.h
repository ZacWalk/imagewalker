#pragma once

// A minimal console test harness shared by every app.
//
// Each app has a test.cpp holding IW_TEST cases. Passing /test on the command
// line makes WinMain run them and exit with the failure count instead of
// creating any windows, which is how these GUI executables get tested from a
// script.
//
//     IW_TEST(FilePathStripsExtension)
//     {
//         IW_CHECK(...);
//     }

#include <windows.h>
#include <tchar.h>
#include <stdio.h>

namespace IW
{
namespace Test
{

struct Case
{
	const char *name;
	void (*run)();
	Case *next;
};

namespace Detail
{

inline Case *&Head()
{
	static Case *head = nullptr;
	return head;
}

inline int &CaseFailures()
{
	static int failures = 0;
	return failures;
}

// These are /SUBSYSTEM:WINDOWS executables, so the CRT never binds stdout even
// when the caller redirected it. Writing to the handle directly is the only
// thing that behaves the same for a console, a file and a pipe -- reopening
// CONOUT$ instead would put the results on screen and hand a script an empty
// file.
inline HANDLE Output()
{
	static const HANDLE handle = []
	{
		const HANDLE inherited = ::GetStdHandle(STD_OUTPUT_HANDLE);

		if (inherited != nullptr && inherited != INVALID_HANDLE_VALUE &&
			::GetFileType(inherited) != FILE_TYPE_UNKNOWN)
		{
			return inherited;
		}

		if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::GetConsoleWindow() == nullptr)
			::AllocConsole();

		return ::CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	}();

	return handle;
}

inline void Print(const char *format, ...)
{
	char text[2048];

	va_list args;
	va_start(args, format);
	int length = vsnprintf(text, sizeof(text), format, args);
	va_end(args);

	if (length <= 0)
		return;

	// vsnprintf returns the length it WANTED on truncation, not what it wrote.
	if (static_cast<size_t>(length) >= sizeof(text))
		length = static_cast<int>(sizeof(text)) - 1;

	DWORD written = 0;
	::WriteFile(Output(), text, static_cast<DWORD>(length), &written, nullptr);
}

} // namespace Detail

struct Registrar
{
	explicit Registrar(Case &item)
	{
		item.next = Detail::Head();
		Detail::Head() = &item;
	}
};

inline void Fail(const char *expression, const char *file, int line)
{
	++Detail::CaseFailures();
	Detail::Print("\r\n    FAIL %s(%d): %s", file, line, expression);
}

#define IW_TEST(iw_test_name)                                                     \
	static void iw_test_name();                                                   \
	static ::IW::Test::Case iw_test_case_##iw_test_name =                         \
		{ #iw_test_name, &iw_test_name, nullptr };                                \
	static ::IW::Test::Registrar iw_test_reg_##iw_test_name(                      \
		iw_test_case_##iw_test_name);                                             \
	static void iw_test_name()

#define IW_CHECK(iw_expr)                                                         \
	do                                                                            \
	{                                                                             \
		if (!(iw_expr))                                                           \
			::IW::Test::Fail(#iw_expr, __FILE__, __LINE__);                       \
	} while (0)

#define IW_CHECK_EQ(iw_a, iw_b)                                                   \
	do                                                                            \
	{                                                                             \
		if (!((iw_a) == (iw_b)))                                                  \
			::IW::Test::Fail(#iw_a " == " #iw_b, __FILE__, __LINE__);             \
	} while (0)

// True when the command line asks for test mode. Accepts /test and -test.
// The switch has to start a token and be outside quotes, or shell-opening a
// file from a folder named "photos-test 2024" runs the suite instead.
inline bool IsRequested(LPCTSTR commandLine)
{
	bool atTokenStart = true;
	bool inQuotes = false;

	for (LPCTSTR p = commandLine; p != nullptr && *p != 0; ++p)
	{
		if (*p == _T('"'))
		{
			inQuotes = !inQuotes;
			atTokenStart = false;
			continue;
		}

		if (*p == _T(' ') || *p == _T('\t'))
		{
			atTokenStart = !inQuotes;
			continue;
		}

		if (atTokenStart && !inQuotes &&
		    (*p == _T('/') || *p == _T('-')) && _tcsnicmp(p + 1, _T("test"), 4) == 0)
		{
			const TCHAR after = *(p + 5);

			if (after == 0 || after == _T(' ') || after == _T('\t'))
				return true;
		}

		atTokenStart = false;
	}

	return false;
}

// __try may not appear in a function that needs object unwinding, so the guard
// lives on its own.
inline void RunOne(Case *item)
{
	__try
	{
		item->run();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Fail("unhandled structured exception", item->name, 0);
	}
}

// Runs every registered case. Returns the number of failures, which the caller
// should use as the process exit code.
inline int RunAll(const char *appName)
{
	Detail::Print("%s tests\r\n", appName);

	// Registration reverses source order; run them the way they were written.
	Case *ordered = nullptr;

	for (Case *item = Detail::Head(); item != nullptr;)
	{
		Case *next = item->next;
		item->next = ordered;
		ordered = item;
		item = next;
	}

	Detail::Head() = ordered;

	int total = 0;
	int failed = 0;

	for (Case *item = ordered; item != nullptr; item = item->next)
	{
		++total;
		const int before = Detail::CaseFailures();

		Detail::Print("  %-48s", item->name);

		RunOne(item);

		if (Detail::CaseFailures() == before)
		{
			Detail::Print(" ok\r\n");
		}
		else
		{
			Detail::Print("\r\n  %-48s FAILED\r\n", item->name);
			++failed;
		}
	}

	Detail::Print("%d of %d passed, %d assertion failures\r\n",
	              total - failed, total, Detail::CaseFailures());

	return failed;
}

} // namespace Test
} // namespace IW
