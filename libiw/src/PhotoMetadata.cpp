// ImageWalker by Zac Walker
// Reads what a photo states of its taking - its camera, when it was taken and its exposure - through
// libfiles' properties, for every format libfiles scans, as the strings ImageWalker 3.0 shows and
// renames by; and the colour profile it carries, by name, through libfiles' listing of the profile.

#include "ImagingWin32.h"
#include <libiw/Codec.h>

#include <libfiles/icc.h>
#include <libfiles/properties.h>
#include <libfiles/reader.h>
#include <libfiles/scan.h>
#include <libfiles/stream.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>

namespace iw::imaging
{
	namespace
	{
		// A file libfiles reads where it asks, a few blocks of it.
		class FileStream final : public libfiles::stream
		{
		public:
			explicit FileStream(const std::filesystem::path& path) :
				file_(CreateFileW(native_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
			{
				LARGE_INTEGER size{};
				if (file_ != INVALID_HANDLE_VALUE && GetFileSizeEx(file_, &size)) size_ = static_cast<std::uint64_t>(size.QuadPart);
			}

			~FileStream() override
			{
				if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
			}

			FileStream(const FileStream&) = delete;
			FileStream& operator=(const FileStream&) = delete;

			bool is_open() const { return file_ != INVALID_HANDLE_VALUE; }
			std::uint64_t size() override { return size_; }

			std::size_t read(const std::uint64_t offset, void* data, const std::size_t size) override
			{
				OVERLAPPED at{};
				at.Offset = static_cast<DWORD>(offset);
				at.OffsetHigh = static_cast<DWORD>(offset >> 32);
				DWORD got = 0;
				const auto wanted = static_cast<DWORD>(std::min<std::size_t>(size, 0x7FFFFFFF));
				return ReadFile(file_, data, wanted, &got, &at) ? got : 0;
			}

		private:
			HANDLE file_;
			std::uint64_t size_ = 0;
		};

		std::wstring wide(const std::string& utf8)
		{
			const int length = utf8.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
			if (length <= 0) return {};
			std::wstring result(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), length);
			return result;
		}

		// As EXIF writes a date, "YYYY:MM:DD HH:MM:SS", by the camera's clock whatever zone the file states, as
		// Diffractor shows it; as much of it as the file gives.
		std::wstring exif_text(const libfiles::date& d)
		{
			if (!d.valid()) return {};
			if (d.has_time) return std::format(L"{:04}:{:02}:{:02} {:02}:{:02}:{:02}", d.year, d.month, d.day, d.hour, d.minute, d.second);
			if (d.day != 0) return std::format(L"{:04}:{:02}:{:02}", d.year, d.month, d.day);
			if (d.month != 0) return std::format(L"{:04}:{:02}", d.year, d.month);
			return std::format(L"{:04}", d.year);
		}
	}

	PhotoMetadata read_photo_metadata(const std::filesystem::path& path)
	{
		PhotoMetadata result;
		FileStream file(path);
		if (!file.is_open()) return result;

		libfiles::reader reader(file);
		const auto properties = libfiles::read_properties(reader);

		const auto make = wide(properties.camera_make);
		const auto model = wide(properties.camera_model);
		result.camera = make.empty() ? model : model.empty() ? make : make + L" " + model;
		result.dateTaken = exif_text(properties.created);

		// Under a second as 1/n, as a camera shows it, and otherwise in seconds.
		if (properties.exposure_time > 0.0 && properties.exposure_time < 1.0)
			result.exposure = std::format(L"1/{:.0f} s", 1.0 / properties.exposure_time);
		else if (properties.exposure_time >= 1.0)
			result.exposure = std::format(L"{:.1f} s", properties.exposure_time);

		if (properties.f_number > 0.0) result.aperture = std::format(L"f/{:.1f}", properties.f_number);
		if (properties.iso_speed > 0) result.iso = std::format(L"ISO {}", properties.iso_speed);
		if (properties.focal_length > 0.0) result.focalLength = std::format(L"{:.1f} mm", properties.focal_length);
		return result;
	}

	std::wstring read_colour_profile(const std::filesystem::path& path)
	{
		FileStream file(path);
		if (!file.is_open()) return {};

		try
		{
			libfiles::reader reader(file);
			const auto scanned = libfiles::scan(reader);
			if (scanned.metadata.icc.empty()) return {};

			// libfiles' listing of the profile, whose summary names it and recognises its primaries.
			std::string primaries;
			auto summary = false;

			for (const auto& row : libfiles::list_icc(scanned.metadata.icc))
			{
				if (row.container) summary = row.id == "icc.summary";
				else if (summary && row.key == "Profile") return wide(row.value);
				else if (summary && row.key == "Primaries" && row.value != "custom") primaries = row.value;
			}

			return wide(primaries);
		}
		catch (const std::exception&)
		{
			return {};
		}
	}
}
