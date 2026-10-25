// ImageWalker by Zac Walker
// Reporter-compatible coverage of EXIF orientation and of the metadata saves carry over.

#pragma once

#include "Files.h"
#include <libiw/JpegMetadata.h>
#include "TaskConvert.h"
#include "TaskEdit.h"
#include "TaskRunner.h"

#include <objbase.h>
#include <wincodec.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace iw::tests
{
	namespace orientation_fixtures
	{
		inline void release_all(const std::initializer_list<IUnknown*> values)
		{
			for (auto* value : values)
				if (value) value->Release();
		}

		inline std::filesystem::path system_profile(const wchar_t* name)
		{
			wchar_t folder[MAX_PATH]{};
			const UINT length = GetSystemDirectoryW(folder, MAX_PATH);
			if (!length || length >= MAX_PATH) return {};
			auto path = std::filesystem::path(folder) / L"spool" / L"drivers" / L"color" / name;
			std::error_code error;
			return std::filesystem::is_regular_file(path, error) ? path : std::filesystem::path{};
		}

		// What a fixture photo carries besides its stored pixels.
		struct Photo
		{
			GUID container{GUID_ContainerFormatJpeg};
			int orientation{}; // 0 writes no orientation tag
			bool camera{};     // EXIF camera, date, GPS, maker note and pixel size, and XMP orientation
			bool cmyk{};
			std::filesystem::path profile;
			const char* pngText{};
		};

		inline const std::vector<std::uint8_t>& maker_note()
		{
			static const std::vector<std::uint8_t> bytes{'T', 'e', 's', 't', 'C', 'a', 'm', 0, 1, 2, 3, 4, 250, 251, 252, 253};
			return bytes;
		}

		inline bool write_photo(const std::filesystem::path& path, const files::DecodedImage& stored, const Photo& photo)
		{
			IWICImagingFactory* factory = nullptr;
			IWICBitmap* bitmap = nullptr;
			IWICStream* stream = nullptr;
			IWICBitmapEncoder* encoder = nullptr;
			IWICBitmapFrameEncode* frame = nullptr;
			IPropertyBag2* properties = nullptr;
			IWICMetadataQueryWriter* writer = nullptr;
			IWICColorContext* profile = nullptr;
			const UINT stride = static_cast<UINT>(stored.width) * 4;
			bool written = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
					IID_PPV_ARGS(&factory))) &&
				SUCCEEDED(factory->CreateBitmapFromMemory(stored.width, stored.height, GUID_WICPixelFormat32bppBGRA,
					stride, stride * static_cast<UINT>(stored.height),
					reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(stored.pixels.data())), &bitmap)) &&
				SUCCEEDED(factory->CreateStream(&stream)) &&
				SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
				SUCCEEDED(factory->CreateEncoder(photo.container, nullptr, &encoder)) &&
				SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
				SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) && SUCCEEDED(frame->Initialize(properties)) &&
				SUCCEEDED(frame->SetSize(stored.width, stored.height));
			const bool jpeg = IsEqualGUID(photo.container, GUID_ContainerFormatJpeg) != FALSE;
			WICPixelFormatGUID format = photo.cmyk ? GUID_WICPixelFormat32bppCMYK
				: jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
			written = written && SUCCEEDED(frame->SetPixelFormat(&format));
			if (written && !photo.profile.empty())
				written = SUCCEEDED(factory->CreateColorContext(&profile)) &&
					SUCCEEDED(profile->InitializeFromFilename(photo.profile.c_str())) &&
					SUCCEEDED(frame->SetColorContexts(1, &profile));
			if (written && (photo.orientation || photo.camera || photo.pngText))
				written = SUCCEEDED(frame->GetMetadataQueryWriter(&writer));
			const auto set = [&writer](const std::wstring& query, PROPVARIANT value)
			{
				return SUCCEEDED(writer->SetMetadataByName(query.c_str(), &value));
			};
			const auto text = [&set](const std::wstring& query, const char* value)
			{
				PROPVARIANT property{};
				property.vt = VT_LPSTR;
				property.pszVal = const_cast<char*>(value);
				return set(query, property);
			};
			const auto number = [&set](const std::wstring& query, const int value)
			{
				PROPVARIANT property{};
				property.vt = VT_UI2;
				property.uiVal = static_cast<USHORT>(value);
				return set(query, property);
			};
			const std::wstring ifd = jpeg ? L"/app1/ifd" : L"/ifd";
			if (written && photo.orientation) written = number(ifd + L"/{ushort=274}", photo.orientation);
			if (written && photo.camera)
			{
				PROPVARIANT note{};
				note.vt = VT_BLOB;
				note.blob.cbSize = static_cast<ULONG>(maker_note().size());
				note.blob.pBlobData = const_cast<BYTE*>(maker_note().data());
				written = text(ifd + L"/{ushort=271}", "TestCam") && text(ifd + L"/{ushort=272}", "QA-9000") &&
					text(ifd + L"/exif/{ushort=36867}", "2025:07:14 10:20:30") &&
					text(ifd + L"/gps/{ushort=1}", "N") && set(ifd + L"/exif/{ushort=37500}", note) &&
					number(ifd + L"/exif/{ushort=40962}", stored.width) &&
					number(ifd + L"/exif/{ushort=40963}", stored.height);
				if (written && photo.orientation)
				{
					const auto orientation = std::to_wstring(photo.orientation);
					PROPVARIANT xmp{};
					xmp.vt = VT_LPWSTR;
					xmp.pwszVal = const_cast<wchar_t*>(orientation.c_str());
					written = set(jpeg ? L"/xmp/tiff:Orientation" : L"/ifd/xmp/tiff:Orientation", xmp);
				}
			}
			if (written && photo.pngText) written = text(L"/tEXt/{str=Author}", photo.pngText);
			written = written && SUCCEEDED(frame->WriteSource(bitmap, nullptr)) && SUCCEEDED(frame->Commit()) &&
				SUCCEEDED(encoder->Commit());
			release_all({profile, writer, properties, frame, encoder, stream, bitmap, factory});
			return written;
		}

		// Reads a value from a photo's first frame as text; numbers are decimal, absent is empty.
		inline std::wstring property_text(const std::filesystem::path& path, const wchar_t* query)
		{
			IWICImagingFactory* factory = nullptr;
			IWICBitmapDecoder* decoder = nullptr;
			IWICBitmapFrameDecode* frame = nullptr;
			IWICMetadataQueryReader* reader = nullptr;
			std::wstring result;
			PROPVARIANT value;
			PropVariantInit(&value);
			if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
				SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
					WICDecodeMetadataCacheOnDemand, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)) &&
				SUCCEEDED(frame->GetMetadataQueryReader(&reader)) && SUCCEEDED(reader->GetMetadataByName(query, &value)))
			{
				if (value.vt == VT_UI2) result = std::to_wstring(value.uiVal);
				else if (value.vt == VT_UI4) result = std::to_wstring(value.ulVal);
				else if (value.vt == VT_LPWSTR && value.pwszVal) result = value.pwszVal;
				else if (value.vt == VT_LPSTR && value.pszVal)
					for (const char* character = value.pszVal; *character; ++character)
						result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*character)));
				else if (value.vt == VT_BLOB)
					for (ULONG index = 0; index < value.blob.cbSize; ++index)
						result += std::format(L"{:02x}", value.blob.pBlobData[index]);
				else result = L"(present)";
			}
			PropVariantClear(&value);
			release_all({reader, frame, decoder, factory});
			return result;
		}

		inline std::wstring hex(const std::vector<std::uint8_t>& bytes)
		{
			std::wstring result;
			for (const auto byte : bytes) result += std::format(L"{:02x}", byte);
			return result;
		}

		// The colour space of each ICC profile a photo carries, such as "RGB " or "CMYK".
		inline std::vector<std::string> profile_spaces(const std::filesystem::path& path)
		{
			IWICImagingFactory* factory = nullptr;
			IWICBitmapDecoder* decoder = nullptr;
			IWICBitmapFrameDecode* frame = nullptr;
			std::vector<std::string> result;
			UINT count = 0;
			if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
				SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
					WICDecodeMetadataCacheOnDemand, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)) &&
				SUCCEEDED(frame->GetColorContexts(0, nullptr, &count)) && count && count < 16)
			{
				std::vector<IWICColorContext*> contexts(count, nullptr);
				bool created = true;
				for (auto*& context : contexts) created = created && SUCCEEDED(factory->CreateColorContext(&context));
				if (created && SUCCEEDED(frame->GetColorContexts(count, contexts.data(), &count)))
					for (auto* context : contexts)
					{
						WICColorContextType type{};
						UINT size = 0;
						if (FAILED(context->GetType(&type)) || type != WICColorContextProfile ||
							FAILED(context->GetProfileBytes(0, nullptr, &size)) || size < 20) continue;
						std::vector<BYTE> bytes(size);
						if (SUCCEEDED(context->GetProfileBytes(size, bytes.data(), &size)))
							result.emplace_back(reinterpret_cast<const char*>(bytes.data()) + 16, 4);
					}
				for (auto* context : contexts)
					if (context) context->Release();
			}
			release_all({frame, decoder, factory});
			return result;
		}

		inline std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file) return {};
			std::vector<std::uint8_t> bytes(static_cast<size_t>(file.tellg()));
			file.seekg(0);
			if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
			return bytes;
		}

		inline bool write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			return static_cast<bool>(file);
		}

		struct Segment
		{
			std::uint8_t marker{};
			std::vector<std::uint8_t> payload;
		};

		// The segments before the first scan, read independently of the code under test.
		inline std::vector<Segment> header_segments(const std::vector<std::uint8_t>& bytes)
		{
			std::vector<Segment> result;
			size_t at = 2;
			while (at + 4 <= bytes.size() && bytes[at] == 0xFF && bytes[at + 1] != 0xDA)
			{
				const size_t length = static_cast<size_t>(bytes[at + 2]) << 8 | bytes[at + 3];
				if (length < 2 || at + 2 + length > bytes.size()) break;
				result.push_back({bytes[at + 1], {bytes.begin() + static_cast<std::ptrdiff_t>(at + 4),
					bytes.begin() + static_cast<std::ptrdiff_t>(at + 2 + length)}});
				at += 2 + length;
			}
			return result;
		}

		inline bool starts(const std::vector<std::uint8_t>& payload, const std::string_view prefix)
		{
			return payload.size() >= prefix.size() && std::memcmp(payload.data(), prefix.data(), prefix.size()) == 0;
		}

		inline void append_segment(std::vector<std::uint8_t>& bytes, const std::uint8_t marker,
		                           const std::vector<std::uint8_t>& payload)
		{
			const size_t length = payload.size() + 2;
			bytes.insert(bytes.end(), {0xFF, marker, static_cast<std::uint8_t>(length >> 8),
				static_cast<std::uint8_t>(length & 0xFF)});
			bytes.insert(bytes.end(), payload.begin(), payload.end());
		}

		inline std::vector<std::uint8_t> payload(const std::string_view identifier, const std::string_view body)
		{
			std::vector<std::uint8_t> result(identifier.begin(), identifier.end());
			result.insert(result.end(), body.begin(), body.end());
			return result;
		}

		// IFD0 at offset 8 holds the orientation and the EXIF directory link and points on to IFD1,
		// the thumbnail directory; the EXIF directory at 38 holds the pixel size, as a SHORT and a
		// LONG. In the segment, the orientation value is at 24, the IFD1 link at 40, the width
		// at 54 and the height at 66.
		inline std::vector<std::uint8_t> exif_segment(const bool little, const std::uint32_t orientationType = 3,
		                                              const std::uint32_t firstDirectory = 8)
		{
			std::vector<std::uint8_t> bytes{'E', 'x', 'i', 'f', 0, 0};
			const auto u16 = [&bytes, little](const std::uint32_t value)
			{
				for (int index = 0; index < 2; ++index)
					bytes.push_back(static_cast<std::uint8_t>(value >> 8 * (little ? index : 1 - index)));
			};
			const auto u32 = [&bytes, little](const std::uint32_t value)
			{
				for (int index = 0; index < 4; ++index)
					bytes.push_back(static_cast<std::uint8_t>(value >> 8 * (little ? index : 3 - index)));
			};
			const auto entry = [&](const std::uint32_t tag, const std::uint32_t type, const std::uint32_t value)
			{
				u16(tag);
				u16(type);
				u32(1);
				if (type == 4) u32(value);
				else
				{
					u16(value);
					u16(0);
				}
			};
			bytes.push_back(little ? 'I' : 'M');
			bytes.push_back(little ? 'I' : 'M');
			u16(42);
			u32(firstDirectory);
			u16(2);
			entry(274, orientationType, 6);
			entry(34665, 4, 38);
			u32(68);
			u16(2);
			entry(40962, 3, 4000);
			entry(40963, 4, 3000);
			u32(0);
			u16(1);
			entry(513, 4, 0);
			u32(0);
			return bytes;
		}

		inline std::uint32_t read_number(const std::vector<std::uint8_t>& bytes, const size_t at, const size_t size,
		                                 const bool little)
		{
			std::uint32_t value = 0;
			for (size_t index = 0; index < size && at + index < bytes.size(); ++index)
				value |= static_cast<std::uint32_t>(bytes[at + index]) << 8 * (little ? index : size - 1 - index);
			return value;
		}
	}

	template<class Reporter>
	void test_photo_orientation(Reporter& reporter)
	{
		using namespace orientation_fixtures;
		reporter.section(L"EXIF orientation turns every decode upright");
		const files::DecodedImage stored{3, 2, {0, 1, 2, 3, 4, 5}, 3, 2};
		const std::array<std::vector<std::uint32_t>, 9> upright{{
			{}, {0, 1, 2, 3, 4, 5}, {2, 1, 0, 5, 4, 3}, {5, 4, 3, 2, 1, 0}, {3, 4, 5, 0, 1, 2},
			{0, 3, 1, 4, 2, 5}, {3, 0, 4, 1, 5, 2}, {5, 2, 4, 1, 3, 0}, {2, 5, 1, 4, 0, 3}
		}};
		bool everyOrientation = true;
		for (int orientation = 1; orientation <= 8; ++orientation)
		{
			auto image = stored;
			const bool sideways = orientation >= 5;
			everyOrientation = everyOrientation && files::orient_upright(image, orientation) &&
				image.pixels == upright[orientation] && image.width == (sideways ? 2 : 3) &&
				image.height == (sideways ? 3 : 2) && image.originalWidth == image.width &&
				image.originalHeight == image.height;
		}
		reporter.check(everyOrientation,
			L"all eight EXIF orientations turn stored pixels upright and swap the dimensions of 5 to 8");
		auto unknown = stored;
		reporter.check(files::orient_upright(unknown, 0) && files::orient_upright(unknown, 9) &&
			unknown.pixels == stored.pixels && unknown.width == 3,
			L"orientation values outside 1 to 8 leave the pixels as stored");
		auto malformed = stored;
		malformed.pixels.resize(5);
		reporter.check(!files::orient_upright(malformed, 6) && malformed.width == 3 && malformed.pixels.size() == 5,
			L"a pixel buffer smaller than its dimensions is refused unchanged");

		files::DecodedImage large;
		large.width = large.originalWidth = 70;
		large.height = large.originalHeight = 45;
		for (int y = 0; y < large.height; ++y)
			for (int x = 0; x < large.width; ++x) large.pixels.push_back(static_cast<std::uint32_t>(y * 1000 + x));
		bool tiled = true;
		for (int orientation = 5; orientation <= 8; ++orientation)
		{
			auto image = large;
			tiled = tiled && files::orient_upright(image, orientation) && image.width == 45 && image.height == 70;
			for (int y = 0; tiled && y < image.height; ++y)
				for (int x = 0; tiled && x < image.width; ++x)
				{
					const int sourceX = orientation <= 6 ? y : large.width - 1 - y;
					const int sourceY = orientation == 5 || orientation == 8 ? x : large.height - 1 - x;
					tiled = image.pixels[static_cast<size_t>(y) * image.width + x] ==
						static_cast<std::uint32_t>(sourceY * 1000 + sourceX);
				}
		}
		reporter.check(tiled, L"turning an image larger than one tile maps every pixel");

		const auto root = files::unique_destination(std::filesystem::current_path() / L"iw30-orientation-tests");
		std::error_code error;
		if (root.empty() || !std::filesystem::create_directory(root, error))
		{
			reporter.check(false, L"the orientation fixture directory can be reserved");
			return;
		}
		struct Cleanup
		{
			std::filesystem::path path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
		} cleanup{root};

		files::DecodedImage pattern;
		pattern.width = pattern.originalWidth = 6;
		pattern.height = pattern.originalHeight = 4;
		for (std::uint32_t index = 0; index < 24; ++index) pattern.pixels.push_back(0xff000000u | index * 0x0a0907u);
		bool decoded = true, sized = true, thumbnails = true, areas = true;
		for (int orientation = 1; orientation <= 8; ++orientation)
		{
			const auto path = root / std::format(L"orientation-{}.tif", orientation);
			Photo photo;
			photo.container = GUID_ContainerFormatTiff;
			photo.orientation = orientation;
			if (!write_photo(path, pattern, photo))
			{
				decoded = false;
				continue;
			}
			auto expected = pattern;
			files::orient_upright(expected, orientation);
			const auto image = files::load_image(path);
			decoded = decoded && files::read_orientation(path) == orientation && image.pixels == expected.pixels &&
				image.width == expected.width && image.originalHeight == expected.height;
			int width = 0, height = 0;
			sized = sized && files::read_dimensions(path, width, height) && width == expected.width &&
				height == expected.height;
			const auto thumbnail = files::load_thumbnail(path, 3, 3);
			const bool sideways = orientation >= 5;
			thumbnails = thumbnails && thumbnail.width == (sideways ? 2 : 3) && thumbnail.height == (sideways ? 3 : 2) &&
				thumbnail.pixels.size() == 6 && thumbnail.originalWidth == expected.width &&
				thumbnail.originalHeight == expected.height;
			const auto area = files::load_image_for_area(path, 1, 1);
			areas = areas && area.width == (sideways ? 1 : 2) && area.height == (sideways ? 2 : 1) &&
				area.pixels.size() == 2 && area.originalWidth == expected.width;
		}
		reporter.check(decoded, L"full decodes of TIFF photos in all eight orientations are upright and exact");
		reporter.check(sized, L"dimension probes report the upright size, as sorting and Convert's review need");
		reporter.check(thumbnails, L"thumbnails fit their bound once upright and report the upright full size");
		reporter.check(areas, L"scaled viewer decodes are upright and report the upright full size");

		// A camera JPEG: the stored left half is red, so upright its top half is red.
		files::DecodedImage halves;
		halves.width = halves.originalWidth = 32;
		halves.height = halves.originalHeight = 16;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 32; ++x) halves.pixels.push_back(x < 16 ? 0xffff0000u : 0xff0000ffu);
		const auto camera = root / L"camera.jpg";
		Photo photo;
		photo.orientation = 6;
		reporter.check(write_photo(camera, halves, photo), L"a JPEG stored on its side can be written");
		const auto turned = files::load_image(camera);
		const auto red = [&turned](const int x, const int y)
		{
			const auto pixel = turned.pixels[static_cast<size_t>(y) * turned.width + x];
			return (pixel >> 16 & 0xff) > 200 && (pixel & 0xff) < 60;
		};
		reporter.check(turned.width == 16 && turned.height == 32 && turned.pixels.size() == 512 &&
			red(8, 4) && !red(8, 28), L"a JPEG with orientation 6 is shown turned clockwise");
	}

	template<class Reporter, class EditorAccess = EditAccess>
	void test_metadata_carry(Reporter& reporter)
	{
		using namespace orientation_fixtures;
		reporter.section(L"Saves carry metadata and reset the orientation");
		const auto root = files::unique_destination(std::filesystem::current_path() / L"iw30-metadata-carry-tests");
		std::error_code error;
		if (root.empty() || !std::filesystem::create_directory(root, error))
		{
			reporter.check(false, L"the metadata fixture directory can be reserved");
			return;
		}
		struct Cleanup
		{
			std::filesystem::path path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
		} cleanup{root};

		files::DecodedImage square;
		square.width = square.height = square.originalWidth = square.originalHeight = 16;
		square.pixels.assign(256, 0xff336699u);
		const auto plain = root / L"plain.jpg";
		reporter.check(files::save_image(square, plain, files::ImageSaveFormat::jpeg),
			L"a re-encoded JPEG fixture can be written");
		const auto encoded = read_bytes(plain);

		std::vector<std::uint8_t> source{0xFF, 0xD8};
		append_segment(source, 0xE0, payload({"JFIF\0", 5}, {"\x01\x02\x01\x00\x48\x00\x48\x01\x01\x10\x20\x30", 12}));
		append_segment(source, 0xE1, exif_segment(true));
		append_segment(source, 0xE1, payload({"http://ns.adobe.com/xap/1.0/\0", 29},
			R"(<rdf:Description tiff:Orientation="6"/><tiff:Orientation>8</tiff:Orientation>)"));
		std::vector<std::uint8_t> profile(128, 0);
		std::memcpy(profile.data() + 16, "RGB ", 4);
		auto icc = payload({"ICC_PROFILE\0\x01\x01", 14}, {});
		icc.insert(icc.end(), profile.begin(), profile.end());
		append_segment(source, 0xE2, icc);
		append_segment(source, 0xE2, payload({"MPF\0", 4}, "MM\0*"));
		append_segment(source, 0xED, payload({"Photoshop 3.0\0", 14}, "8BIM"));
		append_segment(source, 0xEE, payload("Adobe", "\x00\x64\x00\x00\x00\x01"));
		append_segment(source, 0xFE, payload("kept comment", {}));
		append_segment(source, 0xDB, std::vector<std::uint8_t>(65, 1));

		const auto carried = jpeg::carry_metadata(source, encoded, 20, 70000);
		const auto segments = header_segments(carried);
		const auto find = [&segments](const std::uint8_t marker, const std::string_view prefix) -> const Segment*
		{
			for (const auto& segment : segments)
				if (segment.marker == marker && starts(segment.payload, prefix)) return &segment;
			return nullptr;
		};
		const auto* jfif = find(0xE0, {"JFIF\0", 5});
		const auto* exif = find(0xE1, {"Exif\0\0", 6});
		const auto* xmp = find(0xE1, "http://ns.adobe.com/xap/1.0/");
		reporter.check(!carried.empty() && !segments.empty() && segments.front().marker == 0xE0 && jfif &&
			jfif->payload.size() == 14 && jfif->payload[12] == 0 && jfif->payload[13] == 0 && jfif->payload[9] == 0x48,
			L"the JFIF segment leads, keeps its density and loses its thumbnail");
		reporter.check(exif && read_number(exif->payload, 24, 2, true) == 1 && read_number(exif->payload, 40, 4, true) == 0,
			L"EXIF orientation becomes 1 and the stale thumbnail directory is unlinked");
		reporter.check(exif && read_number(exif->payload, 54, 2, true) == 20 &&
			read_number(exif->payload, 66, 4, true) == 70000,
			L"EXIF pixel dimensions follow the new pixels, in SHORT and LONG entries alike");
		reporter.check(exif && exif->payload.size() == exif_segment(true).size(),
			L"the EXIF block is corrected in place, never re-serialised");
		const std::string xmpText = xmp ? std::string(xmp->payload.begin(), xmp->payload.end()) : std::string();
		reporter.check(xmpText.find(R"(tiff:Orientation="1")") != std::string::npos &&
			xmpText.find("<tiff:Orientation>1<") != std::string::npos,
			L"XMP orientation becomes 1 in both attribute and element form");
		reporter.check(find(0xE2, {"ICC_PROFILE\0", 12}) && !find(0xE2, {"MPF\0", 4}) &&
			find(0xED, "Photoshop 3.0") && !find(0xEE, "Adobe") && find(0xFE, "kept comment"),
			L"an RGB profile, IPTC and comments are kept; the multi-picture index and Adobe transform are dropped");
		const auto tables = std::ranges::count_if(segments, [](const Segment& segment) { return segment.marker == 0xDB; });
		const auto encodedTables = std::ranges::count_if(header_segments(encoded),
			[](const Segment& segment) { return segment.marker == 0xDB; });
		reporter.check(tables == encodedTables && std::equal(encoded.end() - 64, encoded.end(), carried.end() - 64),
			L"the quantisation tables and scan come from the new encoding, not the source");
		const auto carriedPath = root / L"carried.jpg";
		reporter.check(write_bytes(carriedPath, carried) && files::load_image(carriedPath).width == 16 &&
			files::read_orientation(carriedPath) == 1 && property_text(carriedPath, L"/app1/thumb/{ushort=513}").empty(),
			L"the carried JPEG decodes, reads as upright and has no thumbnail");

		auto bigEndian = source;
		const auto exifAt = std::ranges::search(bigEndian, std::string_view("Exif\0\0", 6)).begin() - bigEndian.begin();
		const auto motorola = exif_segment(false);
		std::ranges::copy(motorola, bigEndian.begin() + exifAt);
		const auto carriedMotorola = header_segments(jpeg::carry_metadata(bigEndian, encoded, 20, 30));
		const auto motorolaExif = std::ranges::find_if(carriedMotorola, [](const Segment& segment)
			{ return segment.marker == 0xE1 && starts(segment.payload, {"Exif\0\0", 6}); });
		reporter.check(motorolaExif != carriedMotorola.end() && read_number(motorolaExif->payload, 24, 2, false) == 1 &&
			read_number(motorolaExif->payload, 54, 2, false) == 20 && read_number(motorolaExif->payload, 66, 4, false) == 30,
			L"big-endian EXIF is corrected in its own byte order");

		auto grey = source;
		const auto profileAt = std::ranges::search(grey, std::string_view("RGB ")).begin() - grey.begin();
		std::memcpy(grey.data() + profileAt, "GRAY", 4);
		const auto greySegments = header_segments(jpeg::carry_metadata(grey, encoded, 16, 16));
		reporter.check(!greySegments.empty() && std::ranges::none_of(greySegments, [](const Segment& segment)
			{ return segment.marker == 0xE2 && starts(segment.payload, {"ICC_PROFILE\0", 12}); }),
			L"a profile that does not describe RGB is not attached to RGB pixels");

		std::vector<std::uint8_t> badExif{0xFF, 0xD8};
		append_segment(badExif, 0xE1, exif_segment(true, 3, 4000));
		append_segment(badExif, 0xFE, payload("note", {}));
		std::vector<std::uint8_t> asciiOrientation{0xFF, 0xD8};
		append_segment(asciiOrientation, 0xE1, exif_segment(true, 2));
		const auto withoutExif = [](const std::vector<Segment>& list)
		{
			return !list.empty() && std::ranges::none_of(list, [](const Segment& segment)
				{ return segment.marker == 0xE1 && starts(segment.payload, {"Exif\0\0", 6}); });
		};
		const auto badSegments = header_segments(jpeg::carry_metadata(badExif, encoded, 16, 16));
		reporter.check(withoutExif(badSegments) && std::ranges::any_of(badSegments, [](const Segment& segment)
			{ return segment.marker == 0xFE; }) &&
			withoutExif(header_segments(jpeg::carry_metadata(asciiOrientation, encoded, 16, 16))),
			L"EXIF that cannot be corrected is dropped rather than left claiming the old orientation");
		auto truncated = source;
		truncated.resize(40);
		auto noScan = encoded;
		noScan.resize(20);
		reporter.check(jpeg::carry_metadata(truncated, encoded, 16, 16).empty() &&
			jpeg::carry_metadata(source, noScan, 16, 16).empty() &&
			jpeg::carry_metadata(std::vector<std::uint8_t>{0x89, 'P', 'N', 'G'}, encoded, 16, 16).empty() &&
			jpeg::carry_metadata(source, encoded, 0, 16).empty(),
			L"truncated, scanless and non-JPEG inputs carry nothing");
		reporter.check(jpeg::read_header(plain).size() > 2 && jpeg::read_header(root / L"missing.jpg").empty(),
			L"header reading stops at the first scan and fails cleanly for a missing file");

		const auto srgb = system_profile(L"sRGB Color Space Profile.icm");
		files::DecodedImage halves;
		halves.width = halves.originalWidth = 32;
		halves.height = halves.originalHeight = 16;
		for (int y = 0; y < 16; ++y)
			for (int x = 0; x < 32; ++x) halves.pixels.push_back(x < 16 ? 0xffff0000u : 0xff0000ffu);
		Photo cameraPhoto;
		cameraPhoto.orientation = 6;
		cameraPhoto.camera = true;
		cameraPhoto.profile = srgb;
		const auto sideways = root / L"sideways.jpg";
		reporter.check(write_photo(sideways, halves, cameraPhoto), L"a camera JPEG with full metadata can be written");
		const auto expectedNote = hex(maker_note());
		const auto keeps_camera = [&](const std::filesystem::path& path)
		{
			return property_text(path, L"/app1/ifd/{ushort=271}") == L"TestCam" &&
				property_text(path, L"/app1/ifd/exif/{ushort=36867}") == L"2025:07:14 10:20:30" &&
				property_text(path, L"/app1/ifd/gps/{ushort=1}") == L"N" &&
				property_text(path, L"/app1/ifd/exif/{ushort=37500}") == expectedNote;
		};
		const auto upright_profile = [&](const std::filesystem::path& path)
		{
			return srgb.empty() || profile_spaces(path) == std::vector<std::string>{"RGB "};
		};
		const auto upright = files::load_image(sideways);
		files::SaveOptions everything;
		everything.metadataSource = sideways;
		everything.carry = files::CarryMetadata::all;
		const auto copy = root / L"sideways-copy.jpg";
		reporter.check(upright.width == 16 && files::save_image(upright, copy, files::ImageSaveFormat::jpeg, everything),
			L"an upright photo is saved with its source's metadata");
		reporter.check(files::read_orientation(copy) == 1 && files::load_image(copy).height == 32 &&
			property_text(copy, L"/xmp/tiff:Orientation") == L"1",
			L"the saved JPEG is upright in its pixels and in its EXIF and XMP orientation");
		reporter.check(keeps_camera(copy) && upright_profile(copy) &&
			property_text(copy, L"/app1/ifd/exif/{ushort=40962}") == L"16" &&
			property_text(copy, L"/app1/ifd/exif/{ushort=40963}") == L"32",
			L"camera, date, GPS, the maker note byte for byte, the colour profile and the new size are carried");

		files::SaveOptions appearance = everything;
		appearance.carry = files::CarryMetadata::profile;
		const auto converted = root / L"converted.png";
		const auto convertedJpeg = root / L"converted.jpg";
		reporter.check(files::save_image(upright, converted, files::ImageSaveFormat::png, appearance) &&
			files::save_image(upright, convertedJpeg, files::ImageSaveFormat::jpeg, appearance) &&
			upright_profile(converted) && upright_profile(convertedJpeg) &&
			files::read_metadata(convertedJpeg).camera.empty() && files::read_orientation(convertedJpeg) == 1,
			L"carrying the profile alone keeps colour and writes no camera data or orientation");
		files::SaveOptions missing = everything;
		missing.metadataSource = root / L"vanished.jpg";
		const auto unsourced = root / L"unsourced.jpg";
		const auto bitmap = root / L"profile.bmp";
		reporter.check(files::save_image(upright, unsourced, files::ImageSaveFormat::jpeg, missing) &&
			files::load_image(unsourced).height == 32 &&
			files::save_image(upright, bitmap, files::ImageSaveFormat::bmp, everything) &&
			files::load_image(bitmap).height == 32,
			L"an unreadable source or a format without metadata still saves the pixels");

		const auto tiff = root / L"scan.tif";
		Photo scan;
		scan.container = GUID_ContainerFormatTiff;
		scan.orientation = 8;
		scan.camera = true;
		scan.profile = system_profile(L"RSWOP.icm");
		const bool cmyk = !scan.profile.empty();
		scan.cmyk = cmyk;
		if (!cmyk) scan.profile = srgb;
		reporter.check(write_photo(tiff, halves, scan), L"a TIFF with orientation, camera fields and a profile is written");
		const auto tiffCopy = root / L"scan-copy.tif";
		const auto turnedTiff = files::load_image(tiff);
		everything.metadataSource = tiff;
		reporter.check(turnedTiff.height == 32 &&
			files::save_image(turnedTiff, tiffCopy, files::ImageSaveFormat::tiff, everything) &&
			files::load_image(tiffCopy).pixels == turnedTiff.pixels && files::read_orientation(tiffCopy) == 1 &&
			property_text(tiffCopy, L"/ifd/{ushort=271}") == L"TestCam" &&
			property_text(tiffCopy, L"/ifd/xmp/tiff:Orientation") == L"1",
			L"a TIFF saved into TIFF keeps its metadata, with the orientation reset in EXIF and XMP");
		reporter.check(cmyk ? profile_spaces(tiffCopy).empty() : upright_profile(tiffCopy),
			L"a TIFF's RGB profile is kept and a CMYK one is removed from the RGB output");

		files::DecodedImage screenshot = square;
		const auto png = root / L"screenshot.png";
		Photo screen;
		screen.container = GUID_ContainerFormatPng;
		screen.profile = srgb;
		screen.pngText = "Tester";
		everything.metadataSource = png;
		const auto pngCopy = root / L"screenshot-copy.png";
		reporter.check(write_photo(png, screenshot, screen) &&
			files::save_image(screenshot, pngCopy, files::ImageSaveFormat::png, everything) &&
			property_text(pngCopy, L"/tEXt/{str=Author}") == L"Tester" && upright_profile(pngCopy),
			L"a PNG saved into PNG keeps its text and colour profile");

		const auto edited = root / L"edited.jpg";
		reporter.check(write_photo(edited, halves, cameraPhoto), L"the Photo Edit fixture can be written");
		TaskEdit editor;
		EditorAccess::prepare(editor, edited, files::load_image_for_area(edited, 1024, 1024));
		reporter.check(EditorAccess::write(editor, edited, true), L"an in-place Photo Edit save succeeds");
		const auto backup = root / L"edited-original.jpg";
		reporter.check(files::read_orientation(edited) == 1 && files::load_image(edited).height == 32 &&
			keeps_camera(edited) && upright_profile(edited),
			L"Photo Edit's in-place save writes upright pixels and keeps the photo's metadata and profile");
		reporter.check(files::read_orientation(backup) == 6 && files::load_image(backup).height == 32 &&
			keeps_camera(backup), L"the original backup is the untouched photo");

		convert::Settings settings;
		settings.format = files::ImageSaveFormat::png;
		settings.destination = root / L"converted";
		auto plan = convert::analyze({sideways}, settings);
		const auto review = plan.rows.empty() ? std::wstring{} : convert::describe_changes(plan.rows.front(), settings);
		tasks::run_plan(plan, {}, convert::run_options(settings, {sideways}));
		const auto output = plan.rows.empty() ? std::filesystem::path{} : plan.rows.front().destination;
		const auto result = files::load_image(output);
		reporter.check(review.starts_with(L"16 x 32") && plan.count(tasks::RowState::success) == 1 &&
			result.width == 16 && result.height == 32 && plan.rows.front().change == L"16 x 32",
			L"Convert reviews and writes the upright size");
		reporter.check(files::read_orientation(output) == 1 && files::read_metadata(output).camera.empty() &&
			upright_profile(output), L"Convert writes upright pixels with the colour profile but no camera data");
	}
}
