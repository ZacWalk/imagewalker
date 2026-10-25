#pragma once

#include <libiw/Codec.h>
#include <memory>

namespace iw::imaging
{
	enum class PixelStorage
	{
		indexed1, indexed2, indexed4, indexed8, indexed8Alpha, gray8,
		bgr555, bgr565, bgr24, bgrx32, bgra32
	};

	struct FrameView
	{
		int left{};
		int top{};
		int width{};
		int height{};
		int stride{};
		PixelStorage format{};
		bool bottomUp{true};
		std::span<const std::uint8_t> pixels;
		std::span<const std::uint8_t> paletteBgra;
		std::uint32_t flags{};
		std::uint32_t delay{};
		std::uint32_t disposal{};
		std::uint32_t transparent{};
	};

	enum class MetadataKind { opaque, exif, iptc, xmp, icc, sourceProfile, encodedImage };

	struct MetadataView
	{
		std::uint32_t type{};
		std::span<const std::uint8_t> bytes;
		MetadataKind kind{MetadataKind::opaque};
	};

	// Native rows are DWORD aligned. The destination must not overlap the source.
	void copy_frame_bgra(const FrameView& frame, std::span<std::uint32_t> destination,
	                     std::size_t stride, bool bottomUp);
	PixelImage pixels_from_frame(const FrameView& frame);

	struct DocumentResult;

	// Views borrow storage from this document. Clone before mutating an independent copy.
	class Document
	{
	public:
		Document();
		~Document();
		Document(Document&&) noexcept;
		Document& operator=(Document&&) noexcept;
		Document(const Document&) = delete;
		Document& operator=(const Document&) = delete;
		std::size_t frame_count() const noexcept;
		FrameView frame(std::size_t index) const;
		std::size_t metadata_count() const noexcept;
		MetadataView metadata(std::size_t index) const;
		int orientation() const;
		int original_width() const;
		int original_height() const;
		std::wstring description() const;
		std::wstring format_key() const;
		PixelImage frame_pixels(std::size_t index = 0) const;
		void copy_bgra(std::span<std::uint32_t> destination, std::size_t stride, bool bottomUp,
		               std::size_t index = 0) const;
		Document clone() const;
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
		friend DocumentResult decode_document(std::span<const std::uint8_t>, std::wstring_view, const DecodeOptions&);
		friend DocumentResult decode_document(const std::filesystem::path&, const DecodeOptions&);
	};

	struct DocumentResult : OperationResult
	{
		Document document;
	};

	DocumentResult decode_document(std::span<const std::uint8_t> bytes, std::wstring_view extension = {},
	                               const DecodeOptions& options = {});
	DocumentResult decode_document(const std::filesystem::path& path, const DecodeOptions& options = {});
}
