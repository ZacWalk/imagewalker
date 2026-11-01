// ImageWalker by Zac Walker
// Reads what a photo states of itself, through libfiles' properties for every format libfiles scans, and
// the colour profile it carries, through libfiles' summary of the profile: the file read where libfiles
// asks, a few blocks of it. ImageWalker 3.0 formats what it shows of them.

#include "FileStream.h"
#include <libiw/Codec.h>

#include <libfiles/icc.h>
#include <libfiles/properties.h>
#include <libfiles/reader.h>
#include <libfiles/scan.h>

namespace iw::imaging
{
	libfiles::properties read_properties(const std::filesystem::path& path)
	{
		FileStream file(path);
		if (!file.is_open()) return {};

		libfiles::reader reader(file);
		return libfiles::read_properties(reader);
	}

	std::optional<libfiles::icc_summary> read_colour_profile(const std::filesystem::path& path)
	{
		FileStream file(path);
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
