#pragma once
#include <libiw/Diagnostics.h>
#include "iw/logfile.h"

namespace IW
{
	inline void ConfigureImagingDiagnostics()
	{
		iw::imaging::set_diagnostic_sink([](std::wstring_view message)
		{
			Logging::Warn(_T("libiw: %.*ls"), static_cast<int>(message.size()), message.data());
		});
	}
}
