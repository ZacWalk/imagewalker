// ImageWalker by Zac Walker
// Implements the bounded tools-file reader, executable discovery, and launch template expansion.

#include "Tools.h"

#include "Paths.h"
#include "Platform.h"

#include <algorithm>
#include <cwctype>
#include <format>
#include <fstream>
#include <map>
#include <set>

namespace iw::tools
{
	namespace
	{
		constexpr size_t maximumNesting = 16;
		constexpr size_t maximumDocumentBytes = 256 * 1024;
		constexpr size_t maximumScannedEntries = 20000;

		// A deliberately small JSON reader. src30 takes no third-party dependency, and the only
		// error recovery is "log and ignore this file".
		class Reader
		{
		public:
			explicit Reader(const std::wstring_view text) : text_(text)
			{
			}

			bool failed() const { return failed_; }
			void fail() { failed_ = true; }
			bool finished() { skip_space(); return position_ == text_.size(); }

			void skip_space()
			{
				while (position_ < text_.size() &&
					(text_[position_] == L' ' || text_[position_] == L'\t' || text_[position_] == L'\r' ||
						text_[position_] == L'\n'))
					++position_;
			}

			bool consume(const wchar_t character)
			{
				skip_space();
				if (position_ >= text_.size() || text_[position_] != character) return false;
				++position_;
				return true;
			}

			wchar_t peek()
			{
				skip_space();
				return position_ < text_.size() ? text_[position_] : L'\0';
			}

			std::wstring read_string()
			{
				std::wstring result;
				if (!consume(L'"'))
				{
					failed_ = true;
					return result;
				}
				while (position_ < text_.size())
				{
					const wchar_t character = text_[position_++];
					if (character == L'"')
					{
						for (size_t index = 0; index < result.size(); ++index)
						{
							if (result[index] >= 0xd800 && result[index] <= 0xdbff)
							{
								if (++index == result.size() || result[index] < 0xdc00 || result[index] > 0xdfff)
								{ failed_ = true; return {}; }
							}
							else if (result[index] >= 0xdc00 && result[index] <= 0xdfff)
							{ failed_ = true; return {}; }
						}
						return result;
					}
					if (character != L'\\')
					{
						if (character < L' ')
						{
							failed_ = true;
							return {};
						}
						result.push_back(character);
						continue;
					}
					if (position_ >= text_.size()) break;
					switch (const wchar_t escaped = text_[position_++])
					{
					case L'n': result.push_back(L'\n'); break;
					case L't': result.push_back(L'\t'); break;
					case L'r': result.push_back(L'\r'); break;
					case L'b': result.push_back(L'\b'); break;
					case L'f': result.push_back(L'\f'); break;
					case L'"': result.push_back(L'"'); break;
					case L'\\': result.push_back(L'\\'); break;
					case L'/': result.push_back(L'/'); break;
					case L'u':
						{
							if (text_.size() - position_ < 4) { failed_ = true; return {}; }
							unsigned int value = 0;
							for (int digit = 0; digit < 4; ++digit)
							{
								const wchar_t hex = text_[position_++];
								const int number = hex >= L'0' && hex <= L'9'
									                   ? hex - L'0'
									                   : hex >= L'a' && hex <= L'f'
									                   ? hex - L'a' + 10
									                   : hex >= L'A' && hex <= L'F'
									                   ? hex - L'A' + 10
									                   : -1;
								if (number < 0)
								{
									failed_ = true;
									return result;
								}
								value = value * 16 + static_cast<unsigned int>(number);
							}
							result.push_back(static_cast<wchar_t>(value));
							break;
						}
					default: failed_ = true; return {};
					}
				}
				failed_ = true;
				return result;
			}

			// Steps over any value without keeping it. The nesting limit is what stops a hostile
			// document recursing until the stack runs out.
			void skip_value(const size_t depth = 0)
			{
				if (depth > maximumNesting || failed_)
				{
					failed_ = true;
					return;
				}
				switch (const wchar_t character = peek())
				{
				case L'"': read_string(); return;
				case L'{':
				case L'[':
					{
						const wchar_t close = character == L'{' ? L'}' : L']';
						consume(character);
						if (consume(close)) return;
						for (;;)
						{
							if (failed_) return;
							if (character == L'{')
							{
								read_string();
								if (!consume(L':'))
								{
									failed_ = true;
									return;
								}
							}
							skip_value(depth + 1);
							if (consume(L',')) continue;
							if (!consume(close)) failed_ = true;
							return;
						}
					}
				default:
					{
						const size_t start = position_;
						while (position_ < text_.size())
						{
							const wchar_t scan = text_[position_];
							if (scan == L',' || scan == L'}' || scan == L']' || scan == L' ' || scan == L'\n' ||
								scan == L'\r' || scan == L'\t')
								break;
							++position_;
						}
						const auto token = text_.substr(start, position_ - start);
						if (token == L"true" || token == L"false" || token == L"null") return;
						size_t digit = 0;
						if (digit < token.size() && token[digit] == L'-') ++digit;
						if (digit >= token.size()) { failed_ = true; return; }
						if (token[digit] == L'0') ++digit;
						else
						{
							if (token[digit] < L'1' || token[digit] > L'9') { failed_ = true; return; }
							while (digit < token.size() && token[digit] >= L'0' && token[digit] <= L'9') ++digit;
						}
						const auto digits = [&]
						{
							const size_t begin = digit;
							while (digit < token.size() && token[digit] >= L'0' && token[digit] <= L'9') ++digit;
							if (begin == digit) failed_ = true;
						};
						if (digit < token.size() && token[digit] == L'.') { ++digit; digits(); }
						if (digit < token.size() && (token[digit] == L'e' || token[digit] == L'E'))
						{
							++digit;
							if (digit < token.size() && (token[digit] == L'+' || token[digit] == L'-')) ++digit;
							digits();
						}
						if (digit != token.size()) failed_ = true;
						return;
					}
				}
			}

		private:
			std::wstring_view text_;
			size_t position_{};
			bool failed_{};
		};

		void read_string_array(Reader& reader, std::vector<std::wstring>& into)
		{
			if (!reader.consume(L'[')) { reader.fail(); return; }
			if (reader.consume(L']')) return;
			for (;;)
			{
				into.push_back(reader.read_string());
				if (reader.failed()) return;
				if (reader.consume(L',')) continue;
				if (!reader.consume(L']')) reader.fail();
				return;
			}
		}

		bool read_app(Reader& reader, ToolDeclaration& declaration)
		{
			if (!reader.consume(L'{')) return false;
			if (reader.consume(L'}')) return true;
			bool validFields = true;
			for (;;)
			{
				const auto key = reader.read_string();
				if (reader.failed() || !reader.consume(L':')) return false;
				std::wstring* field = nullptr;
				if (paths::iequals(key, L"exe")) field = &declaration.exe;
				else if (paths::iequals(key, L"invoke")) field = &declaration.invoke;
				else if (paths::iequals(key, L"extensions")) field = &declaration.extensions;
				else if (paths::iequals(key, L"group")) field = &declaration.group;
				else if (paths::iequals(key, L"text")) field = &declaration.text;
				if (field && reader.peek() == L'"') *field = reader.read_string();
				else
				{
					if (field) validFields = false;
					reader.skip_value(3);
				}
				if (reader.failed()) return false;
				if (reader.consume(L',')) continue;
				if (!validFields) declaration = {};
				return reader.consume(L'}');
			}
		}

		struct NameLess
		{
			bool operator()(const std::wstring& left, const std::wstring& right) const
			{
				return paths::icompare(left, right) < 0;
			}
		};

		std::wstring executable_name(const std::wstring_view value)
		{
			std::wstring name(value);
			if (name.empty() || name.find_first_of(L"\\/:\"<>|?*\r\n\t") != std::wstring::npos ||
				name.find(L'\0') != std::wstring::npos || name.back() == L'.' || name.back() == L' ')
				return {};
			if (std::filesystem::path(name).extension().empty()) name += L".exe";
			return paths::iequals(std::filesystem::path(name).extension().wstring(), L".exe") ? name : L"";
		}

		std::optional<std::wstring> decode_utf8(const std::string_view bytes)
		{
			std::wstring result;
			for (size_t index = 0; index < bytes.size();)
			{
				const auto first = static_cast<unsigned char>(bytes[index++]);
				unsigned int value = first;
				unsigned int minimum = 0;
				size_t remaining = 0;
				if (first >= 0xc2 && first <= 0xdf) { value = first & 0x1f; remaining = 1; minimum = 0x80; }
				else if (first >= 0xe0 && first <= 0xef) { value = first & 0x0f; remaining = 2; minimum = 0x800; }
				else if (first >= 0xf0 && first <= 0xf4) { value = first & 7; remaining = 3; minimum = 0x10000; }
				else if (first >= 0x80) return {};
				if (remaining > bytes.size() - index) return {};
				while (remaining--)
				{
					const auto next = static_cast<unsigned char>(bytes[index++]);
					if ((next & 0xc0) != 0x80) return {};
					value = (value << 6) | (next & 0x3f);
				}
				if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return {};
				if (value > 0xffff)
				{
					value -= 0x10000;
					result.push_back(static_cast<wchar_t>(0xd800 + (value >> 10)));
					result.push_back(static_cast<wchar_t>(0xdc00 + (value & 0x3ff)));
				}
				else result.push_back(static_cast<wchar_t>(value));
			}
			return result;
		}
	}

	bool declaration_is_usable(const ToolDeclaration& declaration)
	{
		if (executable_name(declaration.exe).empty() || declaration.invoke.empty() ||
			declaration.invoke.find_first_of(L"\r\n") != std::wstring::npos ||
			declaration.invoke.find(L'\0') != std::wstring::npos) return false;
		const auto start = declaration.invoke.find_first_not_of(L" \t");
		if (start == std::wstring::npos) return false;
		const std::wstring_view command(declaration.invoke.data() + start, declaration.invoke.size() - start);
		const size_t length = command.starts_with(L"{exe-path}") ? 10 :
			command.starts_with(L"\"{exe-path}\"") ? 12 : 0;
		return length && (command.size() == length || command[length] == L' ' || command[length] == L'\t');
	}

	std::vector<std::wstring> split_extensions(const std::wstring_view text)
	{
		std::vector<std::wstring> result;
		std::wstring current;
		const auto flush = [&result, &current]
		{
			if (current.empty()) return;
			std::wstring value = current.front() == L'.' ? current : L"." + current;
			std::ranges::transform(value, value.begin(), towlower);
			if (std::ranges::find(result, value) == result.end()) result.push_back(std::move(value));
			current.clear();
		};
		for (const wchar_t character : text)
		{
			if (character == L',' || character == L';' || iswspace(character)) flush();
			else current.push_back(character);
		}
		flush();
		return result;
	}

	files::ItemKind parse_group(const std::wstring_view text, bool& hasGroup)
	{
		hasGroup = true;
		if (paths::iequals(text, L"photo") || paths::iequals(text, L"image")) return files::ItemKind::image;
		if (paths::iequals(text, L"video")) return files::ItemKind::video;
		if (paths::iequals(text, L"audio")) return files::ItemKind::audio;
		if (paths::iequals(text, L"document")) return files::ItemKind::document;
		hasGroup = false;
		return files::ItemKind::other;
	}

	Configuration parse_configuration(const std::wstring_view text) try
	{
		Configuration configuration;
		if (text.empty() || text.size() > maximumDocumentBytes) return configuration;
		Reader reader(text);
		if (!reader.consume(L'{')) return configuration;
		if (reader.consume(L'}')) return configuration;
		bool sawTools = false;
		for (;;)
		{
			const auto key = reader.read_string();
			if (reader.failed() || !reader.consume(L':')) return {};
			if (!sawTools && paths::iequals(key, L"tools"))
			{
				sawTools = true;
				if (!reader.consume(L'{')) return {};
				if (!reader.consume(L'}')) for (;;)
				{
					const auto member = reader.read_string();
					if (reader.failed() || !reader.consume(L':')) return {};
					if (paths::iequals(member, L"folders"))
					{
						std::vector<std::wstring> folders;
						read_string_array(reader, folders);
						for (auto& folder : folders)
							if (!folder.empty() && folder.find(L'\0') == std::wstring::npos)
								configuration.folders.emplace_back(std::move(folder));
					}
					else if (paths::iequals(member, L"apps"))
					{
						if (!reader.consume(L'[')) return {};
						size_t entry = 0;
						if (!reader.consume(L']'))
							for (;;)
							{
								++entry;
								ToolDeclaration declaration;
								if (reader.peek() != L'{') reader.skip_value(3);
								else if (!read_app(reader, declaration)) return {};
								if (reader.failed()) return {};
								if (declaration_is_usable(declaration))
									configuration.apps.push_back(std::move(declaration));
								else
									configuration.issues.push_back(std::format(
										L"Tool entry {} needs an executable base name and an invoke template beginning with {{exe-path}}.", entry));
								if (reader.consume(L',')) continue;
								if (!reader.consume(L']')) return {};
								break;
							}
					}
					else reader.skip_value(2);
					if (reader.failed()) return {};
					if (reader.consume(L',')) continue;
					if (!reader.consume(L'}')) return {};
					break;
				}
			}
			else reader.skip_value(1);
			if (reader.failed()) return {};
			if (reader.consume(L',')) continue;
			if (!reader.consume(L'}')) return {};
			break;
		}
		configuration.valid = sawTools && !reader.failed() && reader.finished();
		if (!configuration.valid) return {};
		return configuration;
	}
	catch (...)
	{
		return {};
	}

	std::vector<InstalledTool> discover(const Configuration& configuration, const size_t maximumDepth,
		std::vector<std::wstring>* issues)
	{
		std::vector<InstalledTool> result;
		if (!configuration.valid || configuration.apps.empty()) return result;

		std::map<std::wstring, std::filesystem::path, NameLess> found;
		std::set<std::wstring, NameLess> wanted;
		for (const auto& declaration : configuration.apps)
			if (declaration_is_usable(declaration)) wanted.insert(executable_name(declaration.exe));
		if (wanted.empty()) return result;
		size_t scanned = 0;
		for (const auto& root : configuration.folders)
		{
			std::error_code error;
			if (!root.is_absolute() || !std::filesystem::is_directory(files::native_path(root), error))
			{
				if (issues) issues->push_back(L"Tool search folder is unavailable or not absolute: " + root.wstring());
				continue;
			}
			std::vector<std::pair<std::filesystem::path, size_t>> pending{{root, 0}};
			while (!pending.empty() && scanned < maximumScannedEntries)
			{
				const auto [folder, depth] = pending.back();
				pending.pop_back();
				error.clear();
				std::filesystem::directory_iterator iterator(
					files::native_path(folder), std::filesystem::directory_options::skip_permission_denied,
					error);
				if (error) continue;
				const std::filesystem::directory_iterator end;
				while (iterator != end && !error && scanned < maximumScannedEntries)
				{
					const auto entry = *iterator;
					iterator.increment(error);
					++scanned;
					std::error_code entryError;
					const auto attributes = platform::read_file_attributes(entry.path());
					if (!attributes.known || attributes.reparse) continue;
					if (entry.is_directory(entryError))
					{
						if (depth < (std::min)(maximumDepth, size_t{4}))
							pending.emplace_back(entry.path(), depth + 1);
						continue;
					}
					if (!entry.is_regular_file(entryError) || entryError) continue;
					const auto name = entry.path().filename().wstring();
					if (wanted.contains(name)) found.emplace(name, entry.path());
				}
			}
		}

		for (const auto& declaration : configuration.apps)
		{
			if (!declaration_is_usable(declaration)) continue;
			const auto executable = found.find(executable_name(declaration.exe));
			if (executable == found.end())
			{
				if (issues) issues->push_back(L"Executable was not found in the configured search folders: " + declaration.exe);
				continue;
			}
			InstalledTool tool;
			tool.name = declaration.text.empty() ? declaration.exe : declaration.text;
			tool.executable = executable->second;
			tool.invoke = declaration.invoke;
			tool.extensions = split_extensions(declaration.extensions);
			if (!declaration.group.empty()) tool.group = parse_group(declaration.group, tool.hasGroup);
			if (!declaration.group.empty() && !tool.hasGroup && issues)
				issues->push_back(L"Unknown media group for " + declaration.exe + L": " + declaration.group);
			result.push_back(std::move(tool));
		}
		return result;
	}

	std::vector<const InstalledTool*> tools_for(const std::vector<InstalledTool>& installed,
	                                            const std::filesystem::path& path)
	{
		std::vector<const InstalledTool*> result;
		if (path.empty()) return result;
		const auto extension = paths::lowercase_extension(path);
		const auto kind = files::classify(path);
		const auto append = [&result](const InstalledTool& tool)
		{
			if (std::ranges::any_of(result, [&tool](const InstalledTool* existing)
				{ return paths::equal(existing->executable, tool.executable) && existing->invoke == tool.invoke; })) return;
			result.push_back(&tool);
		};
		// Extension tools first; a tool matching both ways is still listed once.
		for (const auto& tool : installed)
			if (std::ranges::any_of(tool.extensions, [&extension](const std::wstring& value)
			{
				return paths::iequals(value, extension);
			}))
				append(tool);
		for (const auto& tool : installed)
		{
			if (!tool.hasGroup || tool.group != kind) continue;
			append(tool);
		}
		return result;
	}

	std::vector<const InstalledTool*> tools_for(const std::vector<InstalledTool>& installed,
		const std::span<const std::filesystem::path> selected)
	{
		if (selected.empty()) return {};
		auto result = tools_for(installed, selected.front());
		for (const auto& path : selected.subspan(1))
		{
			const auto applicable = tools_for(installed, path);
			std::erase_if(result, [&applicable](const InstalledTool* tool)
			{
				return accepts_multiple_items(*tool) && std::ranges::none_of(applicable, [tool](const InstalledTool* candidate)
				{
					return paths::equal(candidate->executable, tool->executable) && candidate->invoke == tool->invoke;
				});
			});
		}
		return result;
	}

	std::wstring build_arguments(const InstalledTool& tool, const std::filesystem::path& item)
	{
		return build_arguments(tool, std::span<const std::filesystem::path>(&item, 1));
	}

	bool accepts_multiple_items(const InstalledTool& tool)
	{
		return tool.invoke.find(L"{item-paths}") != std::wstring::npos;
	}

	std::wstring build_arguments(const InstalledTool& tool, const std::span<const std::filesystem::path> items)
	{
		const auto quote = [](const std::wstring& value)
		{
			std::wstring quoted{L"\""};
			size_t backslashes = 0;
			for (const auto character : value)
			{
				if (character == L'\\') { ++backslashes; continue; }
				quoted.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
				backslashes = 0;
				quoted.push_back(character);
			}
			quoted.append(backslashes * 2, L'\\');
			quoted.push_back(L'"');
			return quoted;
		};
		std::wstring result;
		const std::wstring executable = quote(tool.executable.wstring());
		if (!declaration_is_usable({tool.executable.filename().wstring(), tool.invoke}) ||
			!tool.executable.is_absolute() || items.empty() ||
			std::ranges::any_of(items, [](const auto& item) { return item.empty(); }) ||
			(items.size() > 1 && !accepts_multiple_items(tool))) return {};
		const std::wstring itemPath = quote(items.front().wstring());
		std::wstring itemPaths;
		for (const auto& item : items)
		{
			if (!itemPaths.empty()) itemPaths.push_back(L' ');
			itemPaths += quote(item.wstring());
		}
		size_t index = tool.invoke.find_first_not_of(L" \t");
		index += tool.invoke.compare(index, 12, L"\"{exe-path}\"") == 0 ? 12 : 10;
		while (index < tool.invoke.size() && iswspace(tool.invoke[index])) ++index;
		for (; index < tool.invoke.size();)
		{
			if (tool.invoke.compare(index, 14, L"\"{item-paths}\"") == 0)
			{
				result += itemPaths;
				index += 14;
				continue;
			}
			if (tool.invoke.compare(index, 12, L"{item-paths}") == 0)
			{
				result += itemPaths;
				index += 12;
				continue;
			}
			if (tool.invoke.compare(index, 12, L"\"{exe-path}\"") == 0)
			{
				result += executable;
				index += 12;
				continue;
			}
			if (tool.invoke.compare(index, 13, L"\"{item-path}\"") == 0)
			{
				result += itemPath;
				index += 13;
				continue;
			}
			if (tool.invoke.compare(index, 10, L"{exe-path}") == 0)
			{
				result += executable;
				index += 10;
				continue;
			}
			if (tool.invoke.compare(index, 11, L"{item-path}") == 0)
			{
				result += itemPath;
				index += 11;
				continue;
			}
			result.push_back(tool.invoke[index++]);
		}
		return result;
	}

	void ToolTable::load(const std::filesystem::path& configurationFile) try
	{
		installed_.clear();
		issues_.clear();
		const auto report = [this, &configurationFile](std::wstring message)
		{
			issues_.push_back(std::move(message) + L"\n" + configurationFile.wstring());
			platform::write_diagnostic(issues_.back() + L"\n");
		};
		std::error_code error;
		const auto native = files::native_path(configurationFile);
		if (!std::filesystem::is_regular_file(native, error))
		{
			report(error && error != std::errc::no_such_file_or_directory
				? L"Tool configuration could not be inspected."
				: L"Tool configuration is missing or is not a regular file.");
			return;
		}
		const auto size = std::filesystem::file_size(native, error);
		if (error || size == 0 || size > maximumDocumentBytes)
		{
			report(L"Tool configuration is unreadable, empty or exceeds the 256 KiB limit.");
			return;
		}
		std::ifstream stream(native, std::ios::binary);
		if (!stream)
		{
			report(L"Tool configuration could not be opened.");
			return;
		}
		std::string bytes(static_cast<size_t>(size), '\0');
		stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		if (stream.gcount() != static_cast<std::streamsize>(bytes.size()) ||
			stream.peek() != std::char_traits<char>::eof())
		{
			report(L"Tool configuration changed while it was being read.");
			return;
		}
		if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
		const auto text = decode_utf8(bytes);
		const auto configuration = text ? parse_configuration(*text) : Configuration{};
		if (!configuration.valid)
		{
			report(L"Tool configuration must be valid UTF-8 JSON with a tools object containing folders and apps.");
			return;
		}
		issues_ = configuration.issues;
		installed_ = discover(configuration, 4, &issues_);
		for (const auto& issue : issues_) platform::write_diagnostic(L"tools: " + issue + L"\n");
	}
	catch (const std::filesystem::filesystem_error& error)
	{
		installed_.clear();
		issues_.push_back(L"Tool configuration discovery failed: " + error.path1().wstring());
		platform::write_diagnostic(issues_.back() + L"\n");
	}

	std::wstring ToolTable::issue_summary() const
	{
		if (issues_.empty()) return L"No tool configuration problems were detected.";
		std::wstring result = L"Tool configuration needs attention:\n";
		const size_t shown = (std::min)(issues_.size(), size_t{20});
		for (size_t i = 0; i < shown; ++i) result += L"\n" + issues_[i] + L"\n";
		if (shown != issues_.size())
			result += std::format(L"\nShowing {} of {} issues. See the diagnostic log beside the executable for the complete list.",
				shown, issues_.size());
		return result;
	}
}
