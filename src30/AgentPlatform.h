// ImageWalker by Zac Walker
// Platform boundary for MCP startup, fixture admission and own-window capture.

#pragma once

#include "Files.h"

#include <any>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace iw::platform::agent
{
	struct Request
	{
		bool enabled{};
		bool capture{};
		std::filesystem::path root;
		std::string error;
	};

	Request command_line();
	void report_error(std::string_view message);

	struct FixtureFolder
	{
		std::filesystem::path path;
		std::vector<files::FolderItem> items;
	};

	class Fixture
	{
	public:
		static std::unique_ptr<Fixture> open(const std::filesystem::path& root, std::string& error);
		~Fixture();

		// Keys are exact UTF-8 relative folder names, separated by '/'. "." is the root.
		const FixtureFolder* find(std::string_view key) const;

	private:
		struct Locks;
		Fixture();
		std::unique_ptr<Locks> locks_;
		std::map<std::string, FixtureFolder, std::less<>> folders_;
	};

	// Refuse any window except our cloaked app window. Do not allow desktop input to it.
	bool secure_window(const std::any& handle);

	struct Capture
	{
		int width{};
		int height{};
		std::vector<std::uint32_t> pixels;
	};

	// No fallback to a screen DC, desktop capture or a caller-supplied HWND.
	Capture capture_window(const std::any& handle, int maxEdge);
}
