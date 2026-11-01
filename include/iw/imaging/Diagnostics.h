#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <string_view>

namespace iw::imaging
{
	using DiagnosticSink = void (*)(std::wstring_view);
	void set_diagnostic_sink(DiagnosticSink sink) noexcept;
	void report_diagnostic(std::wstring_view message);
}

#include <atomic>
#include <cstdio>

namespace iw::imaging
{
	namespace diagnostics_detail
	{
		inline std::atomic<DiagnosticSink> diagnosticSink{};
	}

	inline void set_diagnostic_sink(const DiagnosticSink sink) noexcept
	{
		using namespace diagnostics_detail;
		diagnosticSink.store(sink, std::memory_order_release);
	}

	inline void report_diagnostic(const std::wstring_view message)
	{
		using namespace diagnostics_detail;
		if (const auto sink = diagnosticSink.load(std::memory_order_acquire))
		{
			sink(message);
			return;
		}
		std::fwprintf(stderr, L"libiw: %.*ls\n", static_cast<int>(message.size()), message.data());
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
