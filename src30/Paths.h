// ImageWalker by Zac Walker
// Declares the single source of truth for path comparison, ordering, and file-name validation.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace iw::paths
{
	// Ordinal case-insensitive comparison. Returns <0, 0 or >0 and never reports a spurious
	// inequality when the platform comparison fails, so it is safe inside a sort comparator.
	int icompare(std::wstring_view left, std::wstring_view right);
	bool iequals(std::wstring_view left, std::wstring_view right);

	// Explorer-style ordering: digit runs compare numerically so img9 sorts before img10.
	int natural_compare(std::wstring_view left, std::wstring_view right);

	bool equal(const std::filesystem::path& left, const std::filesystem::path& right);
	bool contains(const std::filesystem::path& folder, const std::filesystem::path& candidate);

	std::wstring lowercase_extension(const std::filesystem::path& path);

	enum class NameProblem
	{
		none,
		empty,
		illegalCharacter,
		reservedName,
		trailingDotOrSpace,
		separator
	};

	// Validates a bare file name typed by the user. Rejects separators, wildcards, alternate
	// data-stream syntax, reserved device names, and trailing dots or spaces.
	NameProblem validate_file_name(std::wstring_view name);
	const wchar_t* describe(NameProblem problem);
}
