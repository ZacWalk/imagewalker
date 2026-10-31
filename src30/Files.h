// ImageWalker by Zac Walker
// Declares non-UI file enumeration, metadata, and image decoding services.

#pragma once
#include <libiw/Codec.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace iw::files
{
	enum class ItemKind { folder, image, document, video, audio, other };

	enum class SortField { name, modified, type, size, dimensions, duration };

	using imaging::ImageSaveFormat;

	struct Metadata
	{
		std::uintmax_t size{};
		std::filesystem::file_time_type modified{};
		std::wstring camera;
		std::wstring dateTaken;
		std::wstring exposure;
		std::wstring aperture;
		std::wstring iso;
		std::wstring focalLength;
		// The colour profile's name, which read_metadata leaves to read_colour_profile: only a view shows it.
		std::wstring colourProfile;
		bool hasSize{};
		bool hasModified{};
		std::int64_t duration{}; // Media Foundation 100-nanosecond units.
		int videoWidth{};
		int videoHeight{};
		double frameRate{};
		std::wstring codec;
		std::wstring mediaError;
		bool hasDuration{};
	};

	struct FolderItem
	{
		std::filesystem::path path;
		ItemKind kind{ItemKind::other};
		std::uintmax_t size{};
		std::filesystem::file_time_type modified{};
		int width{};
		int height{};
		bool hasSize{};
		bool hasModified{};
		std::int64_t duration{};
		bool hasDuration{};
	};

	using imaging::DecodedImage;
	using imaging::CarryMetadata;
	using imaging::SaveOptions;
	struct FileSnapshot
	{
		bool exists{};
		bool directory{};
		bool readOnly{};
		std::uint64_t volume{};
		std::uint64_t identity{};
		std::uint64_t size{};
		std::uint64_t modified{};
		bool operator==(const FileSnapshot&) const = default;
	};

	std::optional<FileSnapshot> snapshot_file(const std::filesystem::path& path);
	bool matches_snapshot(const std::filesystem::path& path, const FileSnapshot& snapshot);

	using imaging::native_path;
	bool is_supported_image(const std::filesystem::path& path);
	bool is_editable_image(const std::filesystem::path& path);
	ItemKind classify(const std::filesystem::path& path, bool directory = false);

	// Enumeration policy for hidden and system entries. Defaults to false, matching Explorer.
	void set_show_hidden(bool showHidden);
	bool show_hidden();

	std::vector<FolderItem> scan_folder_items(const std::filesystem::path& folder,
	                                          const std::stop_token& stop,
	                                          const std::function<void(size_t, size_t)>& progress = {},
	                                          std::error_code* error = nullptr);
	void sort_items(std::vector<FolderItem>& items, SortField field, bool ascending = true);
	std::vector<std::filesystem::path> scan_folder(const std::filesystem::path& folder,
	                                               const std::stop_token& stop,
	                                               const std::function<void(size_t, size_t)>& progress = {});
	Metadata read_metadata(const std::filesystem::path& path);
	std::wstring read_colour_profile(const std::filesystem::path& path);
	// The upright dimensions: a photo stored on its side reports its height as its width.
	bool read_dimensions(const std::filesystem::path& path, int& width, int& height);
	// The EXIF orientation (1 to 8) the decoder leaves to the reader to apply; 1 when there is
	// none. HEIF reports 1 because its decoder applies the container's rotation itself.
	using imaging::read_orientation;
	// Turns pixels stored with an EXIF orientation into their upright presentation, swapping the
	// dimensions for orientations 5 to 8. Other values change nothing. False, leaving the image
	// unchanged, when the turned copy cannot be allocated or the image is malformed.
	using imaging::orient_upright;
	DecodedImage load_image(const std::filesystem::path& path);
	DecodedImage load_image_for_area(const std::filesystem::path& path, int maximumWidth, int maximumHeight);
	DecodedImage load_thumbnail(const std::filesystem::path& path, int maximumWidth, int maximumHeight);
	using imaging::downsample_bgra_2x;
	using imaging::writable_image_formats;
	using imaging::image_save_format_name;
	using imaging::image_save_extension;

	// Returns an empty path when the bounded search finds no unused name.
	std::filesystem::path next_image_path(const std::filesystem::path& folder, ImageSaveFormat format);
	bool save_image(const DecodedImage& image, const std::filesystem::path& path, ImageSaveFormat format,
	                const SaveOptions& options = {}, bool overwrite = true,
	                const std::function<bool()>& beforeCommit = {});

	// Companion files that must move, copy, and rename with their primary. Only existing ones.
	std::vector<std::filesystem::path> sidecar_paths(const std::filesystem::path& primary);

	// The Auto-rename generator: the proposed path when it is free, otherwise "stem (2).ext" and
	// upwards. Bounded, and returns an empty path rather than an occupied one.
	std::filesystem::path unique_destination(const std::filesystem::path& proposed,
	                                         const std::function<bool(const std::filesystem::path&)>& alsoTaken = {});

	// Never overwrite unless asked to. Both create the destination folder when it is missing.
	std::error_code copy_file_to(const std::filesystem::path& source, const std::filesystem::path& destination,
	                             bool overwrite, const std::function<bool()>& beforeCommit = {});
	std::error_code move_file_to(const std::filesystem::path& source, const std::filesystem::path& destination,
	                             bool overwrite, const std::function<bool()>& beforeCommit = {});

	// Copies a file aside before an irreversible in-place overwrite. Returns the backup path, or
	// an empty path when nothing was written. It never replaces an existing backup.
	std::filesystem::path create_original_backup(const std::filesystem::path& path);

	// Expands a rename template into a bare name, without an extension. A run of '#' is the
	// zero-padded sequence number; "{token}" is a metadata substitution and an unknown token is empty.
	std::wstring expand_tokens(std::wstring_view templateText, const Metadata& metadata,
	                           const std::filesystem::path& path, int sequence);
}
