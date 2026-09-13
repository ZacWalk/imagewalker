// ImageWalker by Zac Walker
// Declares external tool configuration, discovery, and attachment to file types.

#pragma once

#include "Files.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace iw::tools
{
	// One entry from the tools file, after its executable has actually been found on disk.
	struct InstalledTool
	{
		std::wstring name;
		std::filesystem::path executable;
		std::wstring invoke; // template using {exe-path} and {item-path}
		std::vector<std::wstring> extensions; // lowercase, with the leading dot
		files::ItemKind group{files::ItemKind::other};
		bool hasGroup{};
	};

	struct ToolDeclaration
	{
		std::wstring exe;
		std::wstring invoke;
		std::wstring extensions;
		std::wstring group;
		std::wstring text;
	};

	struct Configuration
	{
		std::vector<std::filesystem::path> folders;
		std::vector<ToolDeclaration> apps;
		bool valid{};
		std::vector<std::wstring> issues;
	};

	// A bounded reader for the subset of JSON the schema uses. Never throws; an unreadable or
	// malformed document simply yields an invalid configuration.
	Configuration parse_configuration(std::wstring_view text);

	// Requires an executable base name and an invoke template beginning with {exe-path}.
	// A quoted {exe-path} token is also accepted; arbitrary command names are not.
	bool declaration_is_usable(const ToolDeclaration& declaration);

	std::vector<std::wstring> split_extensions(std::wstring_view text);
	files::ItemKind parse_group(std::wstring_view text, bool& hasGroup);

	// Scans the configured roots to a bounded depth and keeps only executables the configuration
	// names, matched case-insensitively on the base name.
	std::vector<InstalledTool> discover(const Configuration& configuration, size_t maximumDepth = 4,
		std::vector<std::wstring>* issues = nullptr);

	// Extension tools first, then group tools, and never the same tool twice for one file type.
	std::vector<const InstalledTool*> tools_for(const std::vector<InstalledTool>& installed,
	                                            const std::filesystem::path& path);
	std::vector<const InstalledTool*> tools_for(const std::vector<InstalledTool>& installed,
		std::span<const std::filesystem::path> paths);

	// Expands the launch template. The executable is always the discovered path, never text from
	// the configuration, and every path is quoted.
	std::wstring build_arguments(const InstalledTool& tool, const std::filesystem::path& item);
	bool accepts_multiple_items(const InstalledTool& tool);
	std::wstring build_arguments(const InstalledTool& tool, std::span<const std::filesystem::path> items);

	class ToolTable
	{
	public:
		// Reads and discovers. Safe to call on a worker; the caller marshals the result.
		void load(const std::filesystem::path& configurationFile);
		const std::vector<InstalledTool>& installed() const { return installed_; }
		const std::vector<std::wstring>& issues() const { return issues_; }
		std::wstring issue_summary() const;
		void set_installed(std::vector<InstalledTool> installed) { installed_ = std::move(installed); }

	private:
		std::vector<InstalledTool> installed_;
		std::vector<std::wstring> issues_;
	};
}
