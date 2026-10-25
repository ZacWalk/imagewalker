#pragma once

// Post-mortem reporting shared by every imagewalker<ver> executable.
//
// Header-only and deliberately free of TCHAR and of any app header: 2.2 is
// built Unicode and the earlier versions MBCS, so this is written against the
// wide Win32 API and compiles correctly into either. Include it from exactly
// one translation unit per app (main.cpp) -- the state below is process-wide
// either way, but there is nothing else here worth pulling into a second.

#include <windows.h>
// windows.h leaves the version APIs and VS_FIXEDFILEINFO out under
// WIN32_LEAN_AND_MEAN, and minidumpapiset.h below needs the struct.
#include <winver.h>
#include <dbghelp.h>
#include <strsafe.h>

#include <exception>
#include <malloc.h>
#include <new>
#include <new.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "version.lib")

namespace IW
{
namespace Crash
{

// Installs the process-wide handlers. Call once, as the first statement in
// WinMain. Reports land next to the executable, or in %TEMP% if that
// directory is not writable.
//
// Covers: unhandled SEH exceptions, unhandled C++ exceptions (via terminate),
// pure virtual calls, invalid CRT parameters, abort(), and failed operator new.
//
// Set IW_TRACE_THROW=1 in the environment to additionally log every C++ throw
// as it happens, which is how you find an exception that something else
// swallows before the app exits.
void Install(const wchar_t *appName);

// Writes the same report for the calling thread with no exception in flight.
// Only the first report in a process is written; a later caller parks.
void WriteReport(const wchar_t *reason);

// The Microsoft CRT keeps terminate handlers per thread, so every thread the
// app creates has to opt in or an unhandled C++ exception there is silent.
void InstallForThread();

// Appends one line to the report directory's trace log. No-op unless
// IW_TRACE=1 is set. Ordinary printf formatting.
void Trace(const char *format, ...);

namespace detail
{

inline wchar_t g_appName[64] = L"imagewalker";
// longPathAware is declared in the manifest, so MAX_PATH is not the limit.
inline wchar_t g_reportDir[4096] = L"";
inline wchar_t g_exePath[4096] = L"";
inline volatile LONG g_reporting = 0;
inline bool g_traceThrow = false;
inline bool g_trace = false;
inline bool g_symInitialized = false;
inline HANDLE g_log = INVALID_HANDLE_VALUE;

/////////////////////////////////////////////////////////////////////////////
// Output. WriteFile rather than the CRT: by the time we get here the heap may
// be the thing that is broken.

inline void Out(const char *format, ...)
{
	char buf[4096];
	va_list args;
	va_start(args, format);
	const int n = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, format, args);
	va_end(args);

	if (n <= 0)
		return;

	OutputDebugStringA(buf);

	if (g_log != INVALID_HANDLE_VALUE)
	{
		DWORD written = 0;
		WriteFile(g_log, buf, static_cast<DWORD>(n), &written, NULL);
	}
}

/////////////////////////////////////////////////////////////////////////////
// Paths

inline bool IsWritable(const wchar_t *dir)
{
	wchar_t probe[MAX_PATH];
	if (FAILED(StringCchPrintfW(probe, MAX_PATH, L"%s\\iw-write-test.tmp", dir)))
		return false;

	const HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
	                             FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	CloseHandle(h);
	return true;
}

inline void ResolveReportDir()
{
	// Truncation is silent: the return value is the buffer size and, on older
	// systems, the buffer is not terminated. A partial path is worse than none.
	const DWORD length = GetModuleFileNameW(NULL, g_exePath, ARRAYSIZE(g_exePath));
	if (length == 0 || length >= ARRAYSIZE(g_exePath))
		g_exePath[0] = L'\0';

	wcscpy_s(g_reportDir, g_exePath);
	if (wchar_t *slash = wcsrchr(g_reportDir, L'\\'))
		*slash = L'\0';

	if (g_reportDir[0] != L'\0' && IsWritable(g_reportDir))
		return;

	// Documented maximum return is MAX_PATH + 1.
	if (GetTempPathW(MAX_PATH + 1, g_reportDir) == 0)
		g_reportDir[0] = L'\0';

	const size_t len = wcslen(g_reportDir);
	if (len > 1 && g_reportDir[len - 1] == L'\\')
		g_reportDir[len - 1] = L'\0';
}

inline void BuildReportPath(wchar_t *out, size_t cch, const wchar_t *extension)
{
	SYSTEMTIME st;
	GetLocalTime(&st);

	StringCchPrintfW(out, cch, L"%s\\%s-crash-%04u%02u%02u-%02u%02u%02u-%lu.%s",
	                 g_reportDir, g_appName,
	                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
	                 GetCurrentProcessId(), extension);
}

/////////////////////////////////////////////////////////////////////////////
// Report body

inline const char *ExceptionName(DWORD code)
{
	switch (code)
	{
	case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
	case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
	case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
	case EXCEPTION_INVALID_HANDLE:           return "EXCEPTION_INVALID_HANDLE";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
	case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
	case 0xE06D7363:                         return "C++ exception";
	case 0x406D1388:                         return "thread name (debugger)";
	default:                                 return "";
	}
}

inline BOOL CALLBACK ModuleCallback(PCWSTR name, DWORD64 base, ULONG size, PVOID)
{
	Out("  %016llx  %8lu KB  %ls\n", base, size / 1024, name);
	return TRUE;
}

inline void WriteStack(CONTEXT *context, HANDLE thread)
{
	// StackWalk64 writes to the context it is given.
	CONTEXT walk = *context;

	STACKFRAME64 frame = {};
	frame.AddrPC.Offset = walk.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = walk.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = walk.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;

	const HANDLE process = GetCurrentProcess();

	for (int i = 0; i < 96; i++)
	{
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &walk,
		                 NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
			break;

		if (frame.AddrPC.Offset == 0)
			break;

		const DWORD64 pc = frame.AddrPC.Offset;

		char moduleName[64] = "?";
		IMAGEHLP_MODULEW64 mod = {};
		mod.SizeOfStruct = sizeof(mod);
		if (SymGetModuleInfoW64(process, pc, &mod))
			_snprintf_s(moduleName, sizeof(moduleName), _TRUNCATE, "%ls", mod.ModuleName);

		BYTE symbolBuffer[sizeof(SYMBOL_INFO) + 1024] = {};
		SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 1023;

		DWORD64 displacement = 0;
		if (SymFromAddr(process, pc, &displacement, symbol))
			Out("  #%-2d %016llx  %s!%s + 0x%llx", i, pc, moduleName, symbol->Name, displacement);
		else
			Out("  #%-2d %016llx  %s", i, pc, moduleName);

		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct = sizeof(line);
		DWORD lineDisplacement = 0;
		if (SymGetLineFromAddr64(process, pc, &lineDisplacement, &line))
			Out("   [%s @ %lu]", line.FileName, line.LineNumber);

		Out("\n");
	}
}

inline void WriteMiniDump(EXCEPTION_POINTERS *pointers, DWORD faultingThreadId)
{
	wchar_t path[MAX_PATH];
	BuildReportPath(path, MAX_PATH, L"dmp");

	const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
	                                FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return;

	MINIDUMP_EXCEPTION_INFORMATION info = {};
	info.ThreadId = faultingThreadId;
	info.ExceptionPointers = pointers;
	info.ClientPointers = FALSE;

	const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
		MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithUnloadedModules |
		MiniDumpWithProcessThreadData);

	if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
	                      type, pointers ? &info : NULL, NULL, NULL))
		Out("\nMinidump: %ls\n", path);

	CloseHandle(file);
}

inline void WriteVersion()
{
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSizeW(g_exePath, &ignored);
	if (size == 0)
		return;

	BYTE *block = static_cast<BYTE *>(LocalAlloc(LPTR, size));
	if (block == NULL)
		return;

	VS_FIXEDFILEINFO *fixed = NULL;
	UINT fixedLen = 0;
	if (GetFileVersionInfoW(g_exePath, 0, size, block) &&
	    VerQueryValueW(block, L"\\", reinterpret_cast<LPVOID *>(&fixed), &fixedLen) && fixed)
	{
		Out("Version:   %u.%u.%u.%u\n",
		    HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
		    HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
	}

	LocalFree(block);
}

inline void WriteHeader(const wchar_t *reason, DWORD faultingThreadId)
{
	SYSTEMTIME st;
	GetLocalTime(&st);

	Out("Reason:    %ls\n", reason);
	Out("Image:     %ls\n", g_exePath);
	WriteVersion();
	Out("Time:      %04u-%02u-%02u %02u:%02u:%02u (local)\n",
	    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	Out("Process:   %lu    Faulting thread: %lu\n", GetCurrentProcessId(), faultingThreadId);
	Out("Debugger:  %s\n", IsDebuggerPresent() ? "attached" : "none");

#ifdef _DEBUG
	Out("Build:     Debug\n");
#else
	Out("Build:     Release\n");
#endif
	Out("\n");
}

inline void WriteExceptionDetail(EXCEPTION_RECORD *record)
{
	if (record == NULL)
		return;

	Out("Exception: 0x%08lx %s\n", record->ExceptionCode, ExceptionName(record->ExceptionCode));
	Out("Address:   %016llx\n", reinterpret_cast<DWORD64>(record->ExceptionAddress));

	if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
	     record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
	    record->NumberParameters >= 2)
	{
		const char *op = "access";
		switch (record->ExceptionInformation[0])
		{
		case 0: op = "read from"; break;
		case 1: op = "write to"; break;
		case 8: op = "execute at"; break;
		}

		Out("Detail:    attempt to %s %016llx\n", op,
		    static_cast<DWORD64>(record->ExceptionInformation[1]));
	}

	Out("\n");
}

inline void WriteRegisters(const CONTEXT *c)
{
	Out("Registers:\n");
	Out("  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n", c->Rax, c->Rbx, c->Rcx, c->Rdx);
	Out("  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n", c->Rsi, c->Rdi, c->Rbp, c->Rsp);
	Out("  r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n", c->R8, c->R9, c->R10, c->R11);
	Out("  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n", c->R12, c->R13, c->R14, c->R15);
	Out("  rip=%016llx eflags=%08lx\n\n", c->Rip, c->EFlags);
}

// The one path everything funnels into. Runs on the reporting thread.
inline void Report(const wchar_t *reason, EXCEPTION_POINTERS *pointers, HANDLE faultingThread,
                   DWORD faultingThreadId, CONTEXT *context, wchar_t *logPathOut, size_t cch)
{
	BuildReportPath(logPathOut, cch, L"log");

	g_log = CreateFileW(logPathOut, GENERIC_WRITE,
	                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);

	// Raw, CRT-free, so there is always evidence the reporter started.
	if (g_log != INVALID_HANDLE_VALUE)
	{
		static const char kBanner[] = "ImageWalker crash report\r\n========================\r\n\r\n";
		DWORD written = 0;
		WriteFile(g_log, kBanner, sizeof(kBanner) - 1, &written, NULL);
	}

	// Everything that needs no symbol machinery goes out first, so a report
	// survives even if dbghelp cannot cope with the damage.
	WriteHeader(reason, faultingThreadId);

	if (pointers)
		WriteExceptionDetail(pointers->ExceptionRecord);

	WriteRegisters(context);
	FlushFileBuffers(g_log);

	// dbghelp is the most fragile part of all this, and the stack is the most
	// valuable, so it goes first and each stage is isolated.
	__try
	{
		// Install() has normally done this already, so that no LoadLibrary runs
		// at fault time - the parked faulting thread may hold the loader lock.
		if (!g_symInitialized)
		{
			SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME |
			              SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
			SymInitializeW(GetCurrentProcess(), g_reportDir, TRUE);
		}
		else
		{
			// Picks up anything loaded since Install(); no loader lock needed.
			SymRefreshModuleList(GetCurrentProcess());
		}

		Out("Stack:\n");
		WriteStack(context, faultingThread);

		Out("\nLoaded modules:\n");
		EnumerateLoadedModulesW64(GetCurrentProcess(), ModuleCallback, NULL);

		if (!g_symInitialized)
			SymCleanup(GetCurrentProcess());
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Out("\n(symbol lookup failed: 0x%08lx)\n", GetExceptionCode());
	}

	FlushFileBuffers(g_log);

	__try
	{
		WriteMiniDump(pointers, faultingThreadId);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Out("\n(minidump failed: 0x%08lx)\n", GetExceptionCode());
	}

	if (g_log != INVALID_HANDLE_VALUE)
	{
		FlushFileBuffers(g_log);
		CloseHandle(g_log);
		g_log = INVALID_HANDLE_VALUE;
	}
}

/////////////////////////////////////////////////////////////////////////////
// Dispatch. The report is produced on a thread created up front, because the
// faulting thread may have no stack left to run any of this on -- which is
// exactly the case for a stack overflow, and often for a fault inside a
// window procedure.

struct PendingReport
{
	// Owned, not aliased: the callers pass a stack buffer that only outlives
	// this if the faulting thread stays parked, which the timeout below breaks.
	wchar_t reason[1024];
	EXCEPTION_POINTERS *pointers;
	CONTEXT context;
	HANDLE thread;
	DWORD threadId;
	wchar_t logPath[MAX_PATH];
};

inline PendingReport g_pending = {};
inline HANDLE g_requestEvent = NULL;
inline HANDLE g_doneEvent = NULL;
inline HANDLE g_reportThread = NULL;
inline DWORD g_reportThreadId = 0;

inline void RunPendingReport()
{
	// The faulting thread is parked on g_doneEvent, so its stack is already
	// stable. Suspending it would risk deadlocking here on a CRT lock it holds.
	Report(g_pending.reason, g_pending.pointers, g_pending.thread, g_pending.threadId,
	       &g_pending.context, g_pending.logPath, MAX_PATH);
}

// Deliberately not run on the faulting thread: MessageBoxW needs a working
// stack (which a stack overflow has just taken away) and pumps messages back
// into the window procedure that is still mid-fault.
inline void ShowReportMessage()
{
	wchar_t message[MAX_PATH + 256];
	StringCchPrintfW(message, ARRAYSIZE(message),
	                 L"%s has stopped: %s\n\nA report was written to:\n%s",
	                 g_appName, g_pending.reason, g_pending.logPath);
	MessageBoxW(NULL, message, g_appName, MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);
}

inline DWORD WINAPI ReportThread(LPVOID)
{
	// set_terminate is per-thread, so without this a throw in here exits silently.
	IW::Crash::InstallForThread();

	if (WaitForSingleObject(g_requestEvent, INFINITE) == WAIT_OBJECT_0)
	{
		RunPendingReport();
		// Signalled once the report is on disk; the dialog outlives that wait.
		SetEvent(g_doneEvent);
		ShowReportMessage();
		return 0;
	}

	SetEvent(g_doneEvent);
	return 0;
}

inline void ReportAndNotify(const wchar_t *reason, EXCEPTION_POINTERS *pointers)
{
	// One report per process. A second faulting thread must not carry on: it
	// would unwind and take the process down mid-report. Parking it costs
	// nothing -- terminating is the reporting thread's job.
	if (InterlockedExchange(&g_reporting, 1) != 0)
	{
		// A second fault on the reporting thread is the report itself failing.
		// Waiting there would be waiting on ourselves.
		if (GetCurrentThreadId() == g_reportThreadId)
			return;

		// Released exactly when the reporter finishes, dialog included. A fixed
		// sleep either truncates a slow minidump or outlives a finished report.
		// Signalled already means the winner is reporting inline instead.
		if (g_reportThread != NULL && WaitForSingleObject(g_reportThread, 0) == WAIT_TIMEOUT)
			WaitForSingleObject(g_reportThread, INFINITE);
		else
			Sleep(90000);

		return;
	}

	StringCchCopyW(g_pending.reason, ARRAYSIZE(g_pending.reason), reason ? reason : L"");
	g_pending.pointers = pointers;
	g_pending.threadId = GetCurrentThreadId();
	g_pending.thread = NULL;
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
	                &g_pending.thread, 0, FALSE, DUPLICATE_SAME_ACCESS);

	if (pointers && pointers->ContextRecord)
		g_pending.context = *pointers->ContextRecord;
	else
		RtlCaptureContext(&g_pending.context);

	// The thread services exactly one request and exits, so a signalled handle
	// means there is nobody left to hand this to and SetEvent would hang us.
	const bool threadAlive = g_reportThread != NULL &&
	                         WaitForSingleObject(g_reportThread, 0) == WAIT_TIMEOUT;

	if (threadAlive && g_requestEvent && g_doneEvent && SetEvent(g_requestEvent))
	{
		// The dedicated thread owns the report from here. If it does not finish
		// the process is too damaged to try again on this stack -- a second
		// Report() would race the first over g_log and over g_pending.
		if (WaitForSingleObject(g_doneEvent, 60000) == WAIT_OBJECT_0)
			WaitForSingleObject(g_reportThread, INFINITE);
	}
	else
	{
		RunPendingReport();
		ShowReportMessage();
	}
}

/////////////////////////////////////////////////////////////////////////////
// Handlers

inline LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS *pointers)
{
	ReportAndNotify(L"unhandled exception", pointers);
	return EXCEPTION_EXECUTE_HANDLER;
}

// A fault raised inside a window procedure never reaches the unhandled filter:
// the kernel's user-callback frame turns it into STATUS_FATAL_USER_CALLBACK_
// EXCEPTION and kills the process. A vectored handler sees it first-chance, so
// that is the only place these can be caught. Restricted to the two codes that
// nothing can recover from: a VEH runs before any __except or catch, and an AV
// that some outer handler was going to deal with must not be reported as a
// crash -- doing so latches g_reporting and disables the reporter for good.
inline LONG CALLBACK FatalFaultFilter(EXCEPTION_POINTERS *pointers)
{
	switch (pointers->ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_STACK_OVERFLOW:
	case 0xC000041DL: // STATUS_FATAL_USER_CALLBACK_EXCEPTION
		break;
	default:
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	ReportAndNotify(L"hardware fault", pointers);
	return EXCEPTION_CONTINUE_SEARCH;
}

// Runs on a C++ exception that nothing caught, which is otherwise a silent exit.
inline void OnTerminate()
{
	const wchar_t *reason = L"unhandled C++ exception";
	wchar_t detail[512];

	try
	{
		if (std::exception_ptr current = std::current_exception())
			std::rethrow_exception(current);
	}
	catch (const std::exception &e)
	{
		StringCchPrintfW(detail, ARRAYSIZE(detail), L"unhandled C++ exception: %hs", e.what());
		reason = detail;
	}
	catch (...)
	{
		reason = L"unhandled C++ exception (non-std type)";
	}

	ReportAndNotify(reason, NULL);
	_exit(3);
}

inline void __cdecl OnPureCall()
{
	ReportAndNotify(L"pure virtual function call", NULL);
	_exit(3);
}

inline void __cdecl OnInvalidParameter(const wchar_t *expression, const wchar_t *function,
                                       const wchar_t *file, unsigned int line, uintptr_t)
{
	wchar_t detail[1024];
	StringCchPrintfW(detail, ARRAYSIZE(detail), L"invalid CRT parameter: %s in %s (%s:%u)",
	                 expression ? expression : L"?", function ? function : L"?",
	                 file ? file : L"?", line);
	ReportAndNotify(detail, NULL);
	_exit(3);
}

inline void __cdecl OnAbort(int)
{
	ReportAndNotify(L"abort() called", NULL);
	_exit(3);
}

inline int __cdecl OnNewFailed(size_t size)
{
	wchar_t detail[128];
	StringCchPrintfW(detail, ARRAYSIZE(detail), L"out of memory allocating %Iu bytes", size);
	ReportAndNotify(detail, NULL);
	_exit(3);
	return 0;
}

// Optional, opt-in: every C++ throw as it happens, so an exception that some
// outer catch(...) swallows still leaves a trace.
inline LONG CALLBACK TraceThrow(EXCEPTION_POINTERS *pointers)
{
	if (pointers->ExceptionRecord->ExceptionCode == 0xE06D7363 && g_reporting == 0)
	{
		void *frames[24] = {};
		const USHORT count = RtlCaptureStackBackTrace(0, ARRAYSIZE(frames), frames, NULL);

		IW::Crash::Trace("C++ throw on thread %lu:", GetCurrentThreadId());
		for (USHORT i = 0; i < count; i++)
			IW::Crash::Trace("    %p", frames[i]);
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace detail

inline void Install(const wchar_t *appName)
{
	using namespace detail;

	if (appName && *appName)
		StringCchCopyW(g_appName, ARRAYSIZE(g_appName), appName);

	ResolveReportDir();

	// Loaded and initialised now, not at fault time: SymInitializeW and
	// MiniDumpWriteDump both LoadLibrary, and the faulting thread parks holding
	// whatever locks it held -- the loader lock among them.
	LoadLibraryW(L"dbghelp.dll");
	LoadLibraryW(L"dbgcore.dll");
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME |
	              SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
	g_symInitialized = SymInitializeW(GetCurrentProcess(), g_reportDir, TRUE) != FALSE;

	g_requestEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	g_doneEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

	g_reportThread = CreateThread(NULL, 512 * 1024, ReportThread, NULL, 0, &g_reportThreadId);

	wchar_t value[8] = L"";
	g_trace = GetEnvironmentVariableW(L"IW_TRACE", value, ARRAYSIZE(value)) > 0 && value[0] == L'1';
	g_traceThrow = GetEnvironmentVariableW(L"IW_TRACE_THROW", value, ARRAYSIZE(value)) > 0 && value[0] == L'1';

	SetUnhandledExceptionFilter(UnhandledFilter);
	AddVectoredExceptionHandler(0, FatalFaultFilter);
	std::set_terminate(OnTerminate);
	_set_purecall_handler(OnPureCall);
	_set_invalid_parameter_handler(OnInvalidParameter);
	_set_new_handler(OnNewFailed);
	signal(SIGABRT, OnAbort);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

	// Without this a fault inside a window procedure is swallowed by the
	// kernel's user-callback frame and the app just vanishes.
	if (const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll"))
	{
		typedef BOOL(WINAPI * SetPolicyFn)(DWORD);
		const SetPolicyFn setPolicy =
			reinterpret_cast<SetPolicyFn>(reinterpret_cast<void *>(
				GetProcAddress(kernel32, "SetProcessUserModeExceptionPolicy")));
		if (setPolicy)
			setPolicy(0);
	}

	if (g_traceThrow)
	{
		g_trace = true;
		AddVectoredExceptionHandler(1, TraceThrow);
	}

	Trace("---- %ls started, pid %lu ----", g_appName, GetCurrentProcessId());
}

inline void WriteReport(const wchar_t *reason)
{
	// Non-fatal diagnostic, so a later caller returns instead of parking the way
	// a faulting thread does. Only the first report in a process is written.
	if (detail::g_reporting != 0)
		return;

	detail::ReportAndNotify(reason ? reason : L"requested report", NULL);
}

// set_terminate is per-thread in the Microsoft CRT.
inline void InstallForThread()
{
	std::set_terminate(detail::OnTerminate);
}

inline void Trace(const char *format, ...)
{
	using namespace detail;

	if (!g_trace)
		return;

	char line[2048];
	va_list args;
	va_start(args, format);
	int n = _vsnprintf_s(line, sizeof(line) - 2, _TRUNCATE, format, args);
	va_end(args);

	if (n <= 0)
		return;

	line[n++] = '\r';
	line[n++] = '\n';
	line[n] = '\0';

	OutputDebugStringA(line);

	wchar_t path[MAX_PATH];
	StringCchPrintfW(path, MAX_PATH, L"%s\\%s-trace.log", g_reportDir, g_appName);

	const HANDLE h = CreateFileW(path, FILE_APPEND_DATA,
	                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;

	DWORD written = 0;
	WriteFile(h, line, static_cast<DWORD>(n), &written, NULL);
	CloseHandle(h);
}

} // namespace Crash
} // namespace IW
