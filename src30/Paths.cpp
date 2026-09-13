// ImageWalker by Zac Walker
// Implements path comparison, Explorer-style natural ordering, and file-name validation.

#include "Platform.h"
#include "Paths.h"

#include <algorithm>
#include <array>
#include <cwctype>

namespace iw::paths
{
	namespace
	{
		bool is_reserved_stem(std::wstring_view stem)
		{
			constexpr std::array reserved{
				L"CON", L"PRN", L"AUX", L"NUL",
				L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
				L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
			};
			return std::ranges::any_of(reserved, [stem](const wchar_t* name) { return iequals(stem, name); });
		}
	}

	int icompare(const std::wstring_view left, const std::wstring_view right)
	{
		return platform::compare_ordinal_ignore_case(left, right);
	}

	bool iequals(const std::wstring_view left, const std::wstring_view right)
	{
		return left.size() == right.size() && icompare(left, right) == 0;
	}

	int natural_compare(const std::wstring_view left, const std::wstring_view right)
	{
		const int logical = platform::compare_file_names(left, right);
		return logical != 0 ? logical : icompare(left, right);
	}

	bool equal(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		return iequals(left.native(), right.native());
	}

	bool contains(const std::filesystem::path& folder, const std::filesystem::path& candidate)
	{
		const auto& parent = folder.native();
		const auto& child = candidate.native();
		if (parent.empty() || child.size() <= parent.size()) return false;
		if (!iequals(std::wstring_view(child).substr(0, parent.size()), parent)) return false;
		const wchar_t boundary = child[parent.size()];
		return boundary == L'\\' || boundary == L'/' || parent.back() == L'\\' || parent.back() == L'/';
	}

	std::wstring lowercase_extension(const std::filesystem::path& path)
	{
		auto extension = path.extension().wstring();
		std::ranges::transform(extension, extension.begin(), towlower);
		return extension;
	}

	NameProblem validate_file_name(const std::wstring_view name)
	{
		if (name.empty()) return NameProblem::empty;
		if (name == L"." || name == L"..") return NameProblem::reservedName;
		for (const wchar_t character : name)
		{
			if (character == L'\\' || character == L'/') return NameProblem::separator;
			if (character < 0x20) return NameProblem::illegalCharacter;
			if (wcschr(L"<>:\"|?*", character)) return NameProblem::illegalCharacter;
		}
		if (name.back() == L'.' || name.back() == L' ') return NameProblem::trailingDotOrSpace;
		const auto dot = name.find(L'.');
		if (is_reserved_stem(dot == std::wstring_view::npos ? name : name.substr(0, dot)))
			return NameProblem::reservedName;
		return NameProblem::none;
	}

	const wchar_t* describe(const NameProblem problem)
	{
		switch (problem)
		{
		case NameProblem::empty: return L"Enter a name for the file.";
		case NameProblem::illegalCharacter: return
				L"A file name cannot contain any of these characters:\n< > : \" | ? *";
		case NameProblem::reservedName: return L"That name is reserved by Windows. Choose a different name.";
		case NameProblem::trailingDotOrSpace: return L"A file name cannot end with a space or a period.";
		case NameProblem::separator: return L"Enter a file name without a folder path.";
		default: return L"";
		}
	}
}
