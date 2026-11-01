#pragma once

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

// ImageWalker by Zac Walker
// Reads what a photo states of itself, through libfiles' properties for every format libfiles scans, and
// the colour profile it carries, through libfiles' summary of the profile: the file read where libfiles
// asks, a few blocks of it. ImageWalker 3.0 formats what it shows of them.

#include <iw/imaging/detail/PlatformFileStream.h>
#include <iw/imaging/Codec.h>

#include <libfiles/icc.h>
#include <libfiles/properties.h>
#include <libfiles/reader.h>
#include <libfiles/scan.h>

namespace iw::imaging
{
	inline libfiles::properties read_properties(const std::filesystem::path& path)
	{
		PlatformFileStream file(path);
		if (!file.is_open()) return {};

		libfiles::reader reader(file);
		return libfiles::read_properties(reader);
	}

	inline std::optional<libfiles::icc_summary> read_colour_profile(const std::filesystem::path& path)
	{
		PlatformFileStream file(path);
		if (!file.is_open()) return {};

		try
		{
			libfiles::reader reader(file);
			const auto scanned = libfiles::scan(reader);
			return libfiles::summarize_icc(scanned.metadata.icc);
		}
		catch (const std::exception&)
		{
			return {};
		}
	}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
