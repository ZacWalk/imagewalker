// ImageWalker by Zac Walker

#include "PlatformWin32.h"
#include "AgentPlatform.h"
#include "Paths.h"

#include <libdesktop/core.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace iw::platform::agent
{
	namespace
	{
		constexpr std::wstring_view markerName = L".imagewalker-mcp-fixture";
		constexpr size_t maxEntries = 256;
		constexpr size_t maxDepth = 8;
		constexpr std::uint64_t maxPhotoBytes = 32 * 1024 * 1024;
		constexpr size_t maxPathLength = 240; // Keep all UI and codec paths in the normal DOS namespace.

		class Handle
		{
		public:
			explicit Handle(HANDLE value) : value_(value) {}
			Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
			~Handle()
			{
				if (*this) CloseHandle(value_);
			}
			explicit operator bool() const { return value_ != INVALID_HANDLE_VALUE; }
			HANDLE get() const { return value_; }

		private:
			HANDLE value_;
		};

		bool ordinary_component(std::wstring_view name)
		{
			// Reject alternate streams, relative components, DOS device names and DOS-normalized spellings.
			return paths::validate_file_name(name) == paths::NameProblem::none;
		}

		bool ordinary_path(const std::filesystem::path& path)
		{
			const auto drive = path.root_name().native();
			return path.is_absolute() && drive.size() == 2 && drive[1] == L':' &&
			       ((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')) &&
			       path.has_relative_path() && path.native().size() <= maxPathLength &&
			       std::ranges::all_of(path.relative_path(),
			                           [](const auto& part) { return ordinary_component(part.native()); });
		}

		bool ordinary_photo(const std::filesystem::path& path)
		{
			const auto ext = paths::lowercase_extension(path);
			return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp";
		}

		Handle hold_path(const std::filesystem::path& path, const bool ancestor)
		{
			// Keep every admitted name in place until the work queues have stopped. Ancestors are shared
			// for writes, as they do not belong to the fixture. The app never enumerates them.
			return Handle(CreateFileW(path.c_str(), ancestor ? FILE_READ_ATTRIBUTES : GENERIC_READ,
			                          ancestor ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, nullptr,
			                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
			                          nullptr));
		}

		bool matches_dos_name(const Handle& handle, const std::filesystem::path& path)
		{
			wchar_t finalPath[maxPathLength + 5]{};
			const auto length = GetFinalPathNameByHandleW(handle.get(), finalPath, std::size(finalPath),
			                                              FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			return length > 4 && length < std::size(finalPath) && std::wstring_view(finalPath, 4) == LR"(\\?\)" &&
			       paths::iequals(finalPath + 4, path.native());
		}

		HWND cloaked_window(const std::any& handle)
		{
			const auto native = std::any_cast<HWND>(&handle);
			const auto window = native ? *native : nullptr;
			DWORD owner = 0;
			DWORD cloaked = 0;
			if (!window || !GetWindowThreadProcessId(window, &owner) || owner != GetCurrentProcessId() ||
			    GetAncestor(window, GA_ROOT) != window ||
			    FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) ||
			    (cloaked & DWM_CLOAKED_APP) == 0)
				return nullptr;
			return window;
		}
	}

	Request command_line()
	{
		int count = 0;
		const auto args = std::unique_ptr<wchar_t*, decltype(&LocalFree)>(CommandLineToArgvW(GetCommandLineW(), &count),
		                                                                  LocalFree);
		Request request;
		if (!args) return request;
		for (int index = 1; index < count; ++index)
		{
			std::wstring_view arg = args.get()[index];
			if (arg.empty() || (arg.front() != L'-' && arg.front() != L'/')) continue;
			const auto letters = arg.find_first_not_of(L"-/");
			if (letters == std::wstring_view::npos) continue;
			arg.remove_prefix(letters);
			const auto switchName = arg.substr(0, arg.find_first_of(L":="));
			if (paths::iequals(switchName, L"mcp") || paths::iequals(switchName, L"fixture-root") ||
			    (switchName.size() > 4 && paths::iequals(switchName.substr(0, 4), L"mcp-")))
				request.enabled = true;
		}
		if (!request.enabled) return request;

		const auto usage = [&]
		{
			request.error = "usage: --mcp --fixture-root <disposable absolute folder> [--mcp-allow-capture]";
			return request;
		};
		if ((count != 4 && count != 5) || std::wstring_view(args.get()[1]) != L"--mcp" ||
		    std::wstring_view(args.get()[2]) != L"--fixture-root" ||
		    (count == 5 && std::wstring_view(args.get()[4]) != L"--mcp-allow-capture"))
			return usage();
		request.root = std::filesystem::path(args.get()[3]).make_preferred();
		if (request.root.filename().empty() && request.root.has_relative_path())
			request.root = request.root.parent_path();
		if (!ordinary_path(request.root)) return usage();
		request.capture = count == 5;
		return request;
	}

	void report_error(const std::string_view message)
	{
		std::fprintf(stderr, "ImageWalker MCP: %.*s\n", static_cast<int>(message.size()), message.data());
	}

	struct Fixture::Locks
	{
		std::vector<Handle> handles;
	};

	Fixture::Fixture() : locks_(std::make_unique<Locks>())
	{
	}
	Fixture::~Fixture() = default;

	std::unique_ptr<Fixture> Fixture::open(const std::filesystem::path& root, std::string& error)
	{
		error = "Fixture must be a named ordinary folder on a local fixed drive, without path aliases.";
		if (!ordinary_path(root) || GetDriveTypeW(root.root_path().c_str()) != DRIVE_FIXED) return {};
		auto fixture = std::unique_ptr<Fixture>(new Fixture());
		const auto open = [&](const std::filesystem::path& path, const bool directory, const bool ancestor,
		                      BY_HANDLE_FILE_INFORMATION& info)
		{
			auto handle = hold_path(path, ancestor);
			if (!handle || GetFileType(handle.get()) != FILE_TYPE_DISK ||
			    !GetFileInformationByHandle(handle.get(), &info) ||
			    (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) != 0 ||
			    ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
			    (!directory && info.nNumberOfLinks != 1) || !matches_dos_name(handle, path))
				return false;
			fixture->locks_->handles.push_back(std::move(handle));
			return true;
		};

		auto current = root.root_path();
		for (const auto& part : root.relative_path())
		{
			current /= part;
			BY_HANDLE_FILE_INFORMATION info{};
			if (!open(current, true, current != root, info)) return {};
		}
		error = "Create an empty .imagewalker-mcp-fixture file in a new disposable fixture root.";
		BY_HANDLE_FILE_INFORMATION marker{};
		if (!open(root / markerName, false, false, marker) || marker.nFileSizeHigh || marker.nFileSizeLow) return {};

		error = "Fixture must contain at most 256 ordinary entries, with up to 8 nested folders and only "
				"single-link PNG, JPEG or BMP photos of at most 32 MiB each (plus the root marker).";
		size_t entries = 0;
		const auto collect = [&](const auto& self, const std::filesystem::path& folder, std::string key,
		                         const size_t depth) -> bool
		{
			if (depth > maxDepth) return false;
			std::error_code ec;
			std::filesystem::directory_iterator it(folder, ec);
			const std::filesystem::directory_iterator end;
			if (ec) return false;
			FixtureFolder collected{folder, {}};
			for (; it != end; it.increment(ec))
			{
				if (ec) return false;
				const auto& path = it->path();
				const auto name = path.filename();
				if (depth == 0 && name == markerName) continue;
				if (++entries > maxEntries || path.native().size() > maxPathLength ||
				    !ordinary_component(name.native()))
					return false;
				const auto attributes = GetFileAttributesW(path.c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES) return false;
				const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				BY_HANDLE_FILE_INFORMATION info{};
				if ((!directory && !ordinary_photo(path)) || !open(path, directory, false, info)) return false;
				files::FolderItem item;
				item.path = path;
				item.kind = directory ? files::ItemKind::folder : files::ItemKind::image;
				if (directory)
				{
					auto childKey = key == "." ? std::string{} : key;
					if (!childKey.empty()) childKey += '/';
					childKey += core::utf16_to_utf8(name.native());
					if (!self(self, path, childKey, depth + 1)) return false;
				}
				else
				{
					item.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
					if (item.size == 0 || item.size > maxPhotoBytes) return false;
					item.hasSize = true;
				}
				collected.items.push_back(std::move(item));
			}
			return !ec && fixture->folders_.emplace(std::move(key), std::move(collected)).second;
		};
		if (!collect(collect, root, ".", 0)) return {};
		error.clear();
		return fixture;
	}

	const FixtureFolder* Fixture::find(std::string_view key) const
	{
		const auto found = folders_.find(key);
		return found == folders_.end() ? nullptr : &found->second;
	}

	bool secure_window(const std::any& handle)
	{
		const auto window = cloaked_window(handle);
		if (!window) return false;
		EnableWindow(window, FALSE);
		return !IsWindowEnabled(window);
	}

	Capture capture_window(const std::any& handle, const int maxEdge)
	{
		const auto window = cloaked_window(handle);
		RECT bounds{};
		if (!window || IsWindowEnabled(window) || IsIconic(window) || !GetWindowRect(window, &bounds)) return {};
		const int width = bounds.right - bounds.left;
		const int height = bounds.bottom - bounds.top;
		if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || maxEdge < 1 ||
		    static_cast<std::uint64_t>(width) * height > 16'000'000)
			return {};

		const auto dc = CreateCompatibleDC(nullptr);
		if (!dc) return {};
		const core::scope_exit deleteDc([dc] { DeleteDC(dc); });
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		void* memory = nullptr;
		const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &memory, nullptr, 0);
		if (!bitmap) return {};
		const core::scope_exit deleteBitmap([bitmap] { DeleteObject(bitmap); });
		const auto previous = SelectObject(dc, bitmap);
		if (!previous || previous == HGDI_ERROR || !memory) return {};
		const core::scope_exit restore([dc, previous] { SelectObject(dc, previous); });

		// The pinned libdesktop helper falls back to copying the desktop. Use only its first leg.
		constexpr UINT renderFullContent = 2;
		RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		if (!PrintWindow(window, dc, renderFullContent)) return {};
		GdiFlush();

		const int edge = (std::max)(width, height);
		const int limit = (std::min)(edge, maxEdge);
		Capture result;
		result.width = (std::max)(1, width * limit / edge);
		result.height = (std::max)(1, height * limit / edge);
		result.pixels.resize(static_cast<size_t>(result.width) * result.height);
		const auto pixels = static_cast<const std::uint32_t*>(memory);
		const auto sample = [&](const int x, const int y)
		{
			return pixels[static_cast<size_t>(y * height / result.height) * width + x * width / result.width] &
			       0x00ffffff;
		};
		for (int y = 0; y < result.height; ++y)
			for (int x = 0; x < result.width; ++x)
				result.pixels[static_cast<size_t>(y) * result.width + x] = sample(x, y);
		if (std::ranges::all_of(result.pixels,
		                        [first = result.pixels.front()](const auto pixel) { return pixel == first; }))
			return {};
		return result;
	}
}
