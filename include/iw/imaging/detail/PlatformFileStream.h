// ImageWalker by Zac Walker
// A file libfiles reads positionally through libdesktop, without excluding external edits.

#pragma once

// Preserve historical min/max macros around inline modern code.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <iw/imaging/Geometry.h>
#include <libfiles/stream.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace iw::imaging
{
	inline df::file_path desktop_file_path(const std::filesystem::path& path)
	{
		const auto utf8 = path.u8string();
		return df::file_path(std::string(
			reinterpret_cast<const char*>(utf8.data()), utf8.size()));
	}

	class PlatformFileStream final : public libfiles::stream
	{
	public:
		explicit PlatformFileStream(const std::filesystem::path& path) :
			file_(::platform::open_file(desktop_file_path(path), ::platform::file_open_mode::read_shared))
		{
			if (file_) size_ = file_->size();
		}

		PlatformFileStream(const PlatformFileStream&) = delete;
		PlatformFileStream& operator=(const PlatformFileStream&) = delete;

		bool is_open() const { return static_cast<bool>(file_); }
		std::uint64_t size() override { return size_; }

		std::size_t read(const std::uint64_t offset, void* data, const std::size_t size) override
		{
			if (!file_ || offset > size_) return 0;
			return static_cast<std::size_t>(file_->read_at(offset,
				static_cast<std::uint8_t*>(data), size));
		}

	private:
		::platform::file_ptr file_;
		std::uint64_t size_ = 0;
	};
}

#pragma pop_macro("max")
#pragma pop_macro("min")
