#pragma once
#include <string_view>

namespace iw::imaging
{
	using DiagnosticSink = void (*)(std::wstring_view);
	void set_diagnostic_sink(DiagnosticSink sink) noexcept;
	void report_diagnostic(std::wstring_view message);
}
