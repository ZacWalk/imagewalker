// ImageWalker by Zac Walker
// Carries a JPEG's metadata segments into a re-encoded JPEG without re-serialising them.

#include <libiw/JpegMetadata.h>
#include <libiw/Codec.h>

#include <libfiles/exif.h>
#include <libfiles/scan.h>

#include <array>
#include <cstring>
#include <fstream>
#include <string_view>
#include <utility>

namespace iw::jpeg
{
	namespace
	{
		constexpr std::uint8_t soi = 0xD8, eoi = 0xD9, sos = 0xDA;
		constexpr std::uint8_t app0 = 0xE0, app1 = 0xE1, app2 = 0xE2, app14 = 0xEE, app15 = 0xEF, comment = 0xFE;
		// Metadata segments hold at most 64 KiB each; a header larger than this is not a photo's.
		constexpr size_t maximumHeaderBytes = 32ull * 1024 * 1024;

		constexpr std::string_view jfifIdentifier{"JFIF\0", 5};
		constexpr std::string_view exifIdentifier{"Exif\0\0", 6};
		constexpr std::string_view xmpIdentifier{"http://ns.adobe.com/xap/1.0/\0", 29};
		constexpr std::string_view iccIdentifier{"ICC_PROFILE\0", 12};
		constexpr std::string_view mpfIdentifier{"MPF\0", 4};

		struct Segment
		{
			std::uint8_t marker{};
			std::span<const std::uint8_t> payload;
		};

		bool is_app(const std::uint8_t marker) { return marker >= app0 && marker <= app15; }

		// Standalone markers have no length and never belong before the first scan.
		bool is_standalone(const std::uint8_t marker)
		{
			return marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= soi);
		}

		bool starts_with(const std::span<const std::uint8_t> payload, const std::string_view prefix)
		{
			return payload.size() >= prefix.size() && std::memcmp(payload.data(), prefix.data(), prefix.size()) == 0;
		}

		// Splits off SOI and every segment before the first scan. `scan` is where the scan starts,
		// or the end of `data` when it holds only a header.
		bool split_header(const std::span<const std::uint8_t> data, std::vector<Segment>& segments, size_t& scan)
		{
			if (data.size() < 2 || data[0] != 0xFF || data[1] != soi) return false;
			size_t position = 2;
			while (position < data.size())
			{
				if (data[position] != 0xFF) return false;
				size_t marker = position + 1;
				while (marker < data.size() && data[marker] == 0xFF) ++marker;
				if (marker >= data.size()) return false;
				const auto code = data[marker];
				if (code == sos || code == eoi)
				{
					scan = position;
					return true;
				}
				if (is_standalone(code) || data.size() - marker < 3) return false;
				const size_t length = static_cast<size_t>(data[marker + 1]) << 8 | data[marker + 2];
				if (length < 2 || length > data.size() - marker - 1) return false;
				segments.push_back({code, data.subspan(marker + 3, length - 2)});
				position = marker + 1 + length;
			}
			scan = position;
			return true;
		}

		// Corrects EXIF for upright pixels of the new size, where its numbers stand: the orientation
		// becomes 1, the pixel dimensions the new ones, and the thumbnail's directory, whose picture
		// shows the old pixels, is unlinked. False when the structure cannot be followed far enough to
		// be sure the orientation it states is 1.
		bool correct_exif(std::vector<std::uint8_t>& payload, const int width, const int height)
		{
			const auto tiff = std::span(payload).subspan(exifIdentifier.size());
			const libfiles::exif_number numbers[] = {
				{libfiles::exif_directory::primary, 0x0112, 1}, // Orientation
				{libfiles::exif_directory::exif, 0xa002, static_cast<std::uint32_t>(width)}, // PixelXDimension
				{libfiles::exif_directory::exif, 0xa003, static_cast<std::uint32_t>(height)}, // PixelYDimension
			};
			libfiles::set_exif_numbers(tiff, numbers);
			if (!libfiles::unlink_exif_thumbnail(tiff)) return false;
			const auto orientation = libfiles::summarize_exif(tiff).orientation;
			return !orientation || *orientation == 1;
		}

		// XMP repeats the orientation as text, as tiff:Orientation="6" or <tiff:Orientation>6<.
		void correct_xmp(std::vector<std::uint8_t>& payload)
		{
			constexpr std::string_view name{"tiff:Orientation"};
			const std::string_view text(reinterpret_cast<const char*>(payload.data()), payload.size());
			const auto skip_space = [&text](size_t at)
			{
				while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at;
				return at;
			};
			for (size_t at = text.find(name); at != std::string_view::npos; at = text.find(name, at + name.size()))
			{
				size_t value = skip_space(at + name.size());
				char close = '<';
				if (value < text.size() && text[value] == '=')
				{
					value = skip_space(value + 1);
					if (value >= text.size() || (text[value] != '"' && text[value] != '\'')) continue;
					close = text[value++];
				}
				else if (value < text.size() && text[value] == '>') ++value;
				else continue;
				if (value + 1 < text.size() && text[value] >= '1' && text[value] <= '8' && text[value + 1] == close)
					payload[value] = '1';
			}
		}

		// ICC_PROFILE chunks carry a sequence number and count before the profile bytes; the
		// first chunk starts with the profile header, whose colour space is at offset 16.
		bool rgb_profile(const std::vector<Segment>& segments)
		{
			constexpr size_t chunkHeader = iccIdentifier.size() + 2;
			for (const auto& segment : segments)
				if (segment.marker == app2 && starts_with(segment.payload, iccIdentifier) &&
					segment.payload.size() >= chunkHeader + 20 && segment.payload[iccIdentifier.size()] == 1)
					return std::memcmp(segment.payload.data() + chunkHeader + 16, "RGB ", 4) == 0;
			return false;
		}
	}

	std::vector<std::uint8_t> read_header(const std::filesystem::path& path)
	{
		std::ifstream file(imaging::native_path(path), std::ios::binary);
		std::vector<std::uint8_t> header(2);
		if (!file.read(reinterpret_cast<char*>(header.data()), 2) || header[0] != 0xFF || header[1] != soi) return {};
		for (;;)
		{
			int value = file.get();
			if (value != 0xFF) return {};
			do value = file.get(); while (value == 0xFF);
			if (value == std::char_traits<char>::eof()) return {};
			const auto marker = static_cast<std::uint8_t>(value);
			if (marker == sos || marker == eoi) return header;
			if (is_standalone(marker)) return {};
			std::array<std::uint8_t, 2> size{};
			if (!file.read(reinterpret_cast<char*>(size.data()), 2)) return {};
			const size_t length = static_cast<size_t>(size[0]) << 8 | size[1];
			if (length < 2 || header.size() + 2 + length > maximumHeaderBytes) return {};
			const size_t start = header.size();
			header.resize(start + 2 + length);
			header[start] = 0xFF;
			header[start + 1] = marker;
			header[start + 2] = size[0];
			header[start + 3] = size[1];
			if (!file.read(reinterpret_cast<char*>(header.data() + start + 4), static_cast<std::streamsize>(length - 2)))
				return {};
		}
	}

	std::vector<std::uint8_t> carry_metadata(const std::span<const std::uint8_t> source,
	                                         const std::span<const std::uint8_t> encoded, const int width,
	                                         const int height)
	{
		std::vector<Segment> sourceSegments, encodedSegments;
		size_t sourceScan{}, encodedScan{};
		if (width <= 0 || height <= 0 || !split_header(source, sourceSegments, sourceScan) ||
			!split_header(encoded, encodedSegments, encodedScan) || encodedScan >= encoded.size())
			return {};
		const bool keepProfile = rgb_profile(sourceSegments);
		std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> kept;
		bool jfif = false, exif = false;
		for (const auto& segment : sourceSegments)
		{
			std::vector<std::uint8_t> payload(segment.payload.begin(), segment.payload.end());
			switch (segment.marker)
			{
			case app0:
				// JFIF keeps its density but not its thumbnail; JFXX holds only thumbnails.
				if (jfif || !starts_with(segment.payload, jfifIdentifier) || payload.size() < 14) continue;
				payload.resize(14);
				payload[12] = payload[13] = 0;
				jfif = true;
				break;
			case app1:
				if (starts_with(segment.payload, exifIdentifier))
				{
					if (exif || !correct_exif(payload, width, height)) continue;
					exif = true;
				}
				else if (starts_with(segment.payload, xmpIdentifier)) correct_xmp(payload);
				break;
			case app2:
				if (starts_with(segment.payload, iccIdentifier) ? !keepProfile : starts_with(segment.payload, mpfIdentifier))
					continue;
				break;
			case app14:
				continue; // The Adobe segment describes the old encoding's colour transform.
			default:
				if (!is_app(segment.marker) && segment.marker != comment) continue;
			}
			kept.emplace_back(segment.marker, std::move(payload));
		}

		std::vector<std::uint8_t> result{0xFF, soi};
		result.reserve(source.size() + encoded.size());
		const auto emit = [&result](const std::uint8_t marker, const std::span<const std::uint8_t> payload)
		{
			const size_t length = payload.size() + 2;
			result.insert(result.end(), {0xFF, marker, static_cast<std::uint8_t>(length >> 8),
				static_cast<std::uint8_t>(length & 0xFF)});
			result.insert(result.end(), payload.begin(), payload.end());
		};
		// A file with neither JFIF nor EXIF keeps the encoder's JFIF identification.
		if (!jfif && !exif)
			for (const auto& segment : encodedSegments)
				if (segment.marker == app0 && starts_with(segment.payload, jfifIdentifier))
				{
					emit(app0, segment.payload);
					break;
				}
		for (const auto& [marker, payload] : kept) emit(marker, payload);
		for (const auto& segment : encodedSegments)
			if (!is_app(segment.marker) && segment.marker != comment) emit(segment.marker, segment.payload);
		result.insert(result.end(), encoded.begin() + static_cast<std::ptrdiff_t>(encodedScan), encoded.end());
		return result;
	}
}
