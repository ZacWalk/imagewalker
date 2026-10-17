#include "ImagingWin32.h"
#include <libiw/Diagnostics.h>
#include <atomic>
#include <cstdio>

namespace iw::imaging
{
	namespace
	{
		std::atomic<DiagnosticSink> diagnosticSink{};
	}

	void set_diagnostic_sink(const DiagnosticSink sink) noexcept
	{
		diagnosticSink.store(sink, std::memory_order_release);
	}

	void report_diagnostic(const std::wstring_view message)
	{
		if (const auto sink = diagnosticSink.load(std::memory_order_acquire))
		{
			sink(message);
			return;
		}
		std::fwprintf(stderr, L"libiw: %.*ls\n", static_cast<int>(message.size()), message.data());
	}
}
