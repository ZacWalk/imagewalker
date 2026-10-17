// ImageWalker by Zac Walker
// Backs up before mutation and refuses undo when any completed state has changed.

#include "Undo.h"

#include "Files.h"
#include "Paths.h"
#include "Platform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <windows.h>
#include <bcrypt.h>

namespace iw::undo
{
	namespace
	{
		std::atomic<History*> configuredHistory{};
		std::once_flag liveHistoryInitialization;
		std::unique_ptr<History> liveHistory;

		struct Handle
		{
			HANDLE value{INVALID_HANDLE_VALUE};
			explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
			~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
			Handle(const Handle&) = delete;
			Handle& operator=(const Handle&) = delete;
			Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
		};

		[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }

		std::wstring explain(const std::exception& error)
		{
			const std::string text = error.what();
			return {text.begin(), text.end()};
		}

		std::wstring unique_id()
		{
			GUID id{};
			wchar_t text[40]{};
			if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, static_cast<int>(std::size(text))))
				fail("Cannot allocate a unique recovery name.");
			return text;
		}

		struct Less
		{
			bool operator()(const std::filesystem::path& a, const std::filesystem::path& b) const
			{ return paths::icompare(a.native(), b.native()) < 0; }
		};

		struct Member
		{
			std::filesystem::path relative;
			files::FileSnapshot snapshot;
			std::array<unsigned char, 32> digest{};
			std::wstring actualName;
			bool operator==(const Member&) const = default;
		};
		using Fingerprint = std::vector<Member>;

		std::filesystem::path absolute_path(const std::filesystem::path& path)
		{
			auto text = path.native();
			if (text.empty()) fail("An undo path is empty.");
			// MSVC decomposes \\?\C:\... at the device prefix, not the drive root. Use
			// DOS/UNC syntax for lexical operations; native_path restores long-path syntax for I/O.
			if (text.size() >= 8 && paths::iequals(std::wstring_view(text).substr(0, 8), LR"(\\?\UNC\)"))
				text = LR"(\\)" + text.substr(8);
			else if (text.starts_with(LR"(\\?\)") && text.size() >= 7 && text[5] == L':' && text[6] == L'\\' &&
				((text[4] >= L'A' && text[4] <= L'Z') || (text[4] >= L'a' && text[4] <= L'z')))
				text.erase(0, 4);
			else if (text.starts_with(LR"(\\?\)") || text.starts_with(LR"(\\.\)"))
				fail("Undo requires a drive or UNC file path, not a device namespace.");
			return std::filesystem::absolute(std::filesystem::path(text)).lexically_normal();
		}

		void no_links(const std::filesystem::path& path)
		{
			const auto absolute = absolute_path(path);
			auto root = absolute.root_path();
			if (absolute.root_name().native().starts_with(LR"(\\)"))
			{
				const auto relative = absolute.relative_path();
				if (relative.empty()) fail("An undo UNC path does not name a share.");
				root /= *relative.begin();
			}
			for (auto part = absolute; !part.empty();)
			{
				Handle probe(CreateFileW(files::native_path(part).c_str(), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
				if (probe.value == INVALID_HANDLE_VALUE)
				{
					const auto error = GetLastError();
					if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
						throw std::filesystem::filesystem_error("Cannot inspect an undo path or parent", part,
							std::error_code(static_cast<int>(error), std::system_category()));
				}
				else
				{
					BY_HANDLE_FILE_INFORMATION info{};
					if (!GetFileInformationByHandle(probe.value, &info))
						throw std::filesystem::filesystem_error("Cannot inspect undo path attributes", part,
							std::error_code(static_cast<int>(GetLastError()), std::system_category()));
					if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
						fail("Undo does not follow symbolic links or reparse points.");
				}
				if (paths::equal(part, root)) break;
				const auto parent = part.parent_path();
				if (parent == part) break;
				part = parent;
			}
		}

		std::array<unsigned char, 32> digest_file(const std::filesystem::path& path)
		{
			Handle file(CreateFileW(files::native_path(path).c_str(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (file.value == INVALID_HANDLE_VALUE) fail("Cannot lock a file for undo verification.");
			BCRYPT_ALG_HANDLE algorithm{};
			if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
				fail("Cannot create the undo content verifier.");
			struct Algorithm
			{
				BCRYPT_ALG_HANDLE value;
				~Algorithm() { BCryptCloseAlgorithmProvider(value, 0); }
			} algorithmOwner{algorithm};
			BCRYPT_HASH_HANDLE hash{};
			if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
				fail("Cannot initialize the undo content verifier.");
			struct Hash
			{
				BCRYPT_HASH_HANDLE value;
				~Hash() { BCryptDestroyHash(value); }
			} hashOwner{hash};
			std::array<unsigned char, 65536> buffer{};
			DWORD read{};
			do
			{
				if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
					fail("Cannot read a file for undo verification.");
				if (read && BCryptHashData(hash, buffer.data(), read, 0) < 0)
					fail("Cannot verify file content.");
			} while (read);
			std::array<unsigned char, 32> result{};
			if (BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) < 0)
				fail("Cannot finish verifying file content.");
			return result;
		}

		Fingerprint fingerprint(const std::filesystem::path& path, const bool recursive = true)
		{
			const auto logicalRoot = absolute_path(path);
			no_links(logicalRoot);
			const auto root = files::snapshot_file(logicalRoot);
			if (!root) fail("Cannot inspect a protected path.");
			Fingerprint result{{{}, *root, {}}};
			if (!root->exists) return result;
			WIN32_FIND_DATAW name{};
			const HANDLE found = FindFirstFileW(files::native_path(logicalRoot).c_str(), &name);
			if (found == INVALID_HANDLE_VALUE) fail("Cannot verify a protected file name.");
			FindClose(found);
			result.front().actualName = name.cFileName;
			if (!root->directory) result.front().digest = digest_file(logicalRoot);
			else if (recursive)
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(files::native_path(logicalRoot)))
				{
					const auto child = absolute_path(entry.path());
					no_links(child);
					const auto state = files::snapshot_file(child);
					if (!state || !state->exists) fail("A protected folder changed while it was inspected.");
					const auto relative = child.lexically_relative(logicalRoot);
					if (!paths::contains(logicalRoot, child) || relative.empty() || relative.is_absolute() ||
						*relative.begin() == L"..")
						fail("A protected folder member is outside its recorded root.");
					result.push_back({relative, *state, state->directory ?
						std::array<unsigned char, 32>{} : digest_file(child)});
				}
				std::sort(result.begin() + 1, result.end(), [](const Member& a, const Member& b)
				{ return Less{}(a.relative, b.relative); });
			}
			if (!files::matches_snapshot(logicalRoot, *root)) fail("A protected path changed while it was inspected.");
			return result;
		}

		bool same_content(const Fingerprint& a, const Fingerprint& b)
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].relative != b[i].relative || a[i].snapshot.exists != b[i].snapshot.exists ||
					a[i].snapshot.directory != b[i].snapshot.directory ||
					a[i].snapshot.size != b[i].snapshot.size || a[i].digest != b[i].digest)
					return false;
			return true;
		}

		void flush_file(const std::filesystem::path& path)
		{
			Handle file(CreateFileW(files::native_path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (file.value == INVALID_HANDLE_VALUE || !FlushFileBuffers(file.value))
				fail("Cannot flush a recovery copy to disk; the operation was refused.");
		}

		void copy_tree(const std::filesystem::path& source, const std::filesystem::path& destination,
			const Fingerprint& expected)
		{
			const auto copy = [&](const std::filesystem::path& from, const std::filesystem::path& to)
			{
				if (const auto error = files::copy_file_to(from, to, false))
					throw std::system_error(error, "Cannot secure an undo copy");
				// A read-only source may produce a read-only copy. Only our newly created copy changes.
				const DWORD attributes = GetFileAttributesW(files::native_path(to).c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES ||
					!SetFileAttributesW(files::native_path(to).c_str(), attributes & ~FILE_ATTRIBUTE_READONLY))
					fail("Cannot prepare the recovery copy for durable flushing.");
				flush_file(to);
			};
			if (!expected.front().snapshot.directory) copy(source, destination);
			else
			{
				if (!std::filesystem::create_directory(files::native_path(destination)))
					fail("The recovery folder name was taken.");
				for (size_t i = 1; i < expected.size(); ++i)
				{
					const auto target = destination / expected[i].relative;
					if (expected[i].snapshot.directory)
					{
						if (!std::filesystem::create_directory(files::native_path(target)))
							fail("A folder appeared in the destination during recovery.");
					}
					else copy(source / expected[i].relative, target);
				}
			}
			if (!same_content(expected, fingerprint(destination)))
				fail("The recovery copy does not match the original.");
		}

		void restore_metadata(const std::filesystem::path& path, const Fingerprint& original)
		{
			for (auto member = original.rbegin(); member != original.rend(); ++member)
			{
				const auto target = member->relative.empty() ? path : path / member->relative;
				Handle file(CreateFileW(files::native_path(target).c_str(), FILE_WRITE_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
				const FILETIME modified{static_cast<DWORD>(member->snapshot.modified),
					static_cast<DWORD>(member->snapshot.modified >> 32)};
				if (file.value == INVALID_HANDLE_VALUE || !SetFileTime(file.value, nullptr, nullptr, &modified))
					fail("Content was restored, but the original timestamp could not be restored.");
				auto attributes = GetFileAttributesW(files::native_path(target).c_str());
				if (attributes == INVALID_FILE_ATTRIBUTES) fail("Cannot inspect restored file attributes.");
				if (member->snapshot.readOnly) attributes |= FILE_ATTRIBUTE_READONLY;
				else attributes &= ~FILE_ATTRIBUTE_READONLY;
				if (!SetFileAttributesW(files::native_path(target).c_str(), attributes))
					fail("Content was restored, but the original attributes could not be restored.");
			}
		}

		void write_record(const std::filesystem::path& folder, const std::wstring& text)
		{
			const auto path = folder / (L"record-" + unique_id() + L".txt");
			Handle file(CreateFileW(files::native_path(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
				nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
			if (file.value == INVALID_HANDLE_VALUE) fail("Cannot create a durable undo recovery record.");
			const std::wstring content = L"\ufeffImageWalker 3.0 recovery record (UTF-16)\r\n"
				L"Automatic Undo is available only in the creating session. Never replay this record automatically.\r\n"
				L"Recovery copies are retained without automatic deletion. Check current files before manual recovery.\r\n" + text;
			const auto bytes = content.size() * sizeof(wchar_t);
			if (bytes > MAXDWORD) fail("The recovery record is too large.");
			DWORD written{};
			if (!WriteFile(file.value, content.data(), static_cast<DWORD>(bytes), &written, nullptr) ||
				written != bytes || !FlushFileBuffers(file.value))
				fail("Cannot flush the undo recovery record; no new mutation is permitted.");
		}

		struct Slot
		{
			std::filesystem::path path;
			std::filesystem::path backup;
			std::filesystem::path parent;
			files::FileSnapshot parentState;
			Fingerprint before;
			Fingerprint after;
			bool directoryOnly{};
			bool restored{};
		};
		struct Entry
		{
			std::wstring description;
			std::filesystem::path folder;
			std::vector<Slot> slots;
			std::vector<std::vector<size_t>> groups;
		};

		// The caller has already established containment using Windows path comparison.
		std::filesystem::path relative_below(const std::filesystem::path& parent,
			const std::filesystem::path& child)
		{
			auto component = child.begin();
			for (auto prefix = parent.begin(); prefix != parent.end(); ++prefix) ++component;
			std::filesystem::path result;
			for (; component != child.end(); ++component) result /= *component;
			return result;
		}

		Fingerprint subtree(const Fingerprint& tree, const std::filesystem::path& relative)
		{
			if (relative.empty()) return tree;
			const auto root = std::ranges::find_if(tree, [&](const Member& member)
			{ return paths::equal(member.relative, relative); });
			if (root == tree.end()) return Fingerprint{{{}, {}, {}}};
			Member first = *root;
			first.actualName = root->relative.filename().wstring();
			first.relative.clear();
			Fingerprint result{std::move(first)};
			if (root->snapshot.directory)
				for (const auto& member : tree)
				{
					if (paths::equal(root->relative, member.relative) ||
						!paths::contains(root->relative, member.relative)) continue;
					auto child = member;
					child.relative = relative_below(root->relative, member.relative);
					result.push_back(std::move(child));
				}
			return result;
		}

		bool verified_restoration(const Fingerprint& original, const Fingerprint& restored)
		{
			if (original.size() != restored.size()) return false;
			for (size_t i = 0; i < original.size(); ++i)
			{
				auto expected = original[i];
				expected.snapshot.identity = restored[i].snapshot.identity;
				if (expected != restored[i]) return false;
			}
			return true;
		}

		void rebase_restored_slot(Slot& earlier, const Slot& restoredSlot, const Fingerprint& restored)
		{
			if (earlier.restored) return;
			const bool exact = paths::equal(earlier.path, restoredSlot.path);
			if (!exact && (!restoredSlot.before.front().snapshot.directory ||
				!paths::contains(restoredSlot.path, earlier.path))) return;
			const auto relative = exact ? std::filesystem::path{} : relative_below(restoredSlot.path, earlier.path);
			const auto originalChild = subtree(restoredSlot.before, relative);
			auto restoredChild = subtree(restored, relative);
			if (earlier.directoryOnly)
			{
				if (earlier.after.size() != 1) return;
				auto expectedNode = earlier.after.front();
				auto originalNode = originalChild.front();
				auto restoredNode = restoredChild.front();
				for (auto* node : {&expectedNode, &originalNode, &restoredNode})
				{
					if (node->snapshot.exists && !node->snapshot.directory) return;
					// A directory-node record never owns its children or their changes to its timestamps.
					node->snapshot.modified = 0;
					node->snapshot.size = 0;
				}
				if (expectedNode != originalNode ||
					!verified_restoration(Fingerprint{originalNode}, Fingerprint{restoredNode})) return;
				restoredChild.resize(1);
			}
			else if (earlier.after != originalChild || !verified_restoration(originalChild, restoredChild)) return;

			if (paths::equal(earlier.parent, restoredSlot.path) ||
				paths::contains(restoredSlot.path, earlier.parent))
			{
				const auto parentRelative = relative_below(restoredSlot.path, earlier.parent);
				const auto oldParent = subtree(restoredSlot.before, parentRelative);
				const auto newParent = subtree(restored, parentRelative);
				const auto& originalParent = oldParent.front().snapshot;
				if (!originalParent.exists || !originalParent.directory ||
					earlier.parentState.volume != originalParent.volume ||
					earlier.parentState.identity != originalParent.identity ||
					!verified_restoration(oldParent, newParent)) return;
				earlier.parentState = newParent.front().snapshot;
			}
			earlier.after = std::move(restoredChild);
		}

		std::wstring slot_text(const Slot& slot)
		{
			return L"Path: " + slot.path.wstring() + L"\r\nOriginal: " +
				(slot.backup.empty() ? L"(absent)" : slot.backup.wstring()) +
				(slot.directoryOnly ? L"\r\nScope: directory node only; children are recorded independently.\r\n" :
					L"\r\nScope: complete item.\r\n");
		}

		std::wstring state_text(const Fingerprint& state)
		{
			std::wstring text;
			constexpr wchar_t hex[] = L"0123456789abcdef";
			for (const auto& member : state)
			{
				text += std::format(L"  {} exists={} directory={} volume={} identity={} size={} modified={} SHA256=",
					member.relative.empty() ? L"(root)" : member.relative.wstring(),
					member.snapshot.exists, member.snapshot.directory, member.snapshot.volume,
					member.snapshot.identity, member.snapshot.size, member.snapshot.modified);
				for (const auto byte : member.digest)
				{
					text += hex[byte >> 4];
					text += hex[byte & 15];
				}
				text += L"\r\n";
			}
			return text;
		}

		// Renaming by the held handle, without replacement, closes the pathname identity race.
		void rename_locked(const Handle& handle, const std::filesystem::path& destination)
		{
			const auto name = std::filesystem::absolute(destination).wstring();
			const auto bytes = name.size() * sizeof(wchar_t);
			if (bytes > MAXDWORD - sizeof(FILE_RENAME_INFO)) fail("A recovery path is too long.");
			std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + bytes);
			auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
			info->ReplaceIfExists = FALSE;
			info->RootDirectory = nullptr;
			info->FileNameLength = static_cast<DWORD>(bytes);
			std::copy(name.begin(), name.end(), info->FileName);
			if (!SetFileInformationByHandle(handle.value, FileRenameInfo, info,
				static_cast<DWORD>(buffer.size())))
				throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
					"Cannot move the verified completed file to recovery storage");
		}

		void remove_empty_directory(const std::filesystem::path& path, const files::FileSnapshot& expected)
		{
			Handle directory(CreateFileW(files::native_path(path).c_str(), FILE_READ_ATTRIBUTES | DELETE,
				FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
			if (directory.value == INVALID_HANDLE_VALUE) fail("Cannot lock the directory for removal.");
			no_links(path);
			const auto current = files::snapshot_file(path);
			if (!current || !current->exists || !current->directory || current->volume != expected.volume ||
				current->identity != expected.identity || current->readOnly != expected.readOnly)
				fail("The directory changed before removal; it was retained.");
			// Unlike a pathname deletion, this targets the verified object and atomically refuses
			// nonempty directories, including a child which arrives after acquiring the handle.
			FILE_DISPOSITION_INFO remove{TRUE};
			if (!SetFileInformationByHandle(directory.value, FileDispositionInfo, &remove, sizeof(remove)))
				throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
					"Directory retained (it may contain unrelated or conflicting files)");
		}
	}

	struct History::State
	{
		explicit State(std::filesystem::path value) : root(std::move(value)) {}
		std::filesystem::path root;
		mutable std::mutex mutex;
		std::vector<std::shared_ptr<Entry>> entries;
		bool active{};
	};

	struct Batch::State
	{
		std::shared_ptr<History::State> history;
		std::shared_ptr<Entry> entry = std::make_shared<Entry>();
		std::map<std::filesystem::path, size_t, Less> lookup;
		bool finished{};
		bool started{};
	};

	History::History(std::filesystem::path root)
		: state_(std::make_shared<State>(absolute_path(root))) {}
	History::~History() = default;
	const std::filesystem::path& History::root() const { return state_->root; }

	std::shared_ptr<Batch> History::begin(std::wstring description)
	{
		return std::shared_ptr<Batch>(new Batch(state_, std::move(description)));
	}

	Batch::Batch(std::shared_ptr<History::State> historyState, std::wstring description)
		: state_(std::make_unique<State>())
	{
		state_->history = std::move(historyState);
		state_->entry->description = std::move(description);
	}

	Batch::~Batch()
	{
		if (!state_->finished && state_->started)
		{
			std::wstring error;
			if (!finish(error)) platform::write_diagnostic(L"Undo: " + error + L"\n");
		}
	}

	std::filesystem::path Batch::recovery_folder() const { return state_->entry->folder; }

	bool Batch::protect(const std::vector<std::filesystem::path>& targets, std::wstring& error)
	{
		return protect_impl(targets, false, error);
	}

	bool Batch::protect_directory(const std::filesystem::path& path, std::wstring& error)
	{
		return protect_impl({path}, true, error);
	}

	bool Batch::remove_directory(const std::filesystem::path& path, std::wstring& error)
	{
		if (!protect_directory(path, error)) return false;
		try
		{
			const auto index = state_->lookup.at(absolute_path(path));
			const auto& slot = state_->entry->slots[index];
			if (!slot.before.front().snapshot.exists) fail("The source directory was not present when protected.");
			remove_empty_directory(slot.path, slot.before.front().snapshot);
			return completed({slot.path}, error);
		}
		catch (const std::exception& failure)
		{
			error = path.wstring() + L": " + explain(failure);
			return false;
		}
	}

	bool Batch::protect_impl(const std::vector<std::filesystem::path>& targets,
		const bool directoryOnly, std::wstring& error)
	{
		try
		{
			if (state_->finished) fail("This undo batch has already finished.");
			if (!state_->started)
			{
				std::lock_guard lock(state_->history->mutex);
				if (state_->history->active) fail("Another file operation or Undo is still active.");
				no_links(state_->history->root);
				std::filesystem::create_directories(files::native_path(state_->history->root));
				state_->entry->folder = state_->history->root / (L"operation-" + unique_id());
				if (!std::filesystem::create_directory(files::native_path(state_->entry->folder)))
					fail("Cannot create the undo recovery folder.");
				write_record(state_->entry->folder, L"Begin: " + state_->entry->description + L"\r\n");
				state_->history->active = true;
				state_->started = true;
			}
			for (const auto& target : targets)
			{
				const auto path = absolute_path(target);
				if (const auto existing = state_->lookup.find(path); existing != state_->lookup.end())
				{
					if (state_->entry->slots[existing->second].directoryOnly != directoryOnly)
						fail("A path cannot be recorded as both a directory node and a complete tree.");
					continue;
				}
				if (paths::equal(path, state_->history->root) || paths::contains(path, state_->history->root) ||
					paths::contains(state_->history->root, path))
					fail("An operation cannot change the application undo storage.");
				Slot slot;
				slot.path = path;
				slot.directoryOnly = directoryOnly;
				slot.before = fingerprint(path, !directoryOnly);
				if (directoryOnly && slot.before.front().snapshot.exists && !slot.before.front().snapshot.directory)
					fail("A directory node is occupied by a file.");
				slot.after = slot.before;
				if (slot.before.front().snapshot.exists)
				{
					slot.backup = state_->entry->folder /
						(std::to_wstring(state_->entry->slots.size()) + L"-original");
					copy_tree(path, slot.backup, slot.before);
					if (fingerprint(path, !directoryOnly) != slot.before) fail("An original changed while its recovery copy was secured.");
				}
				write_record(state_->entry->folder, L"Prepared before mutation\r\n" + slot_text(slot) +
					state_text(slot.before));
				state_->entry->slots.push_back(std::move(slot));
				state_->lookup.emplace(path, state_->entry->slots.size() - 1);
			}
			return true;
		}
		catch (const std::exception& failure)
		{
			error = L"Undo protection failed before writing: " + explain(failure);
			return false;
		}
	}

	bool Batch::validate(const std::vector<std::filesystem::path>& pathsToCheck,
		const bool completedState, std::wstring& error) const
	{
		try
		{
			for (const auto& path : pathsToCheck)
			{
				const auto found = state_->lookup.find(absolute_path(path));
				if (found == state_->lookup.end()) fail("The file was not protected before mutation.");
				const auto& slot = state_->entry->slots[found->second];
				if (fingerprint(slot.path, !slot.directoryOnly) != (completedState ? slot.after : slot.before))
					fail("A protected file changed; the operation was refused.");
				if (completedState)
				{
					const auto parent = files::snapshot_file(slot.parent);
					if (!parent || !parent->exists || !parent->directory ||
						parent->volume != slot.parentState.volume || parent->identity != slot.parentState.identity)
						fail("A completed file's parent changed; the operation was refused.");
				}
			}
			return true;
		}
		catch (const std::exception& failure) { error = explain(failure); return false; }
	}

	bool Batch::validate_before(const std::vector<std::filesystem::path>& pathsToCheck, std::wstring& error) const
	{
		return validate(pathsToCheck, false, error);
	}

	bool Batch::validate_completed(const std::vector<std::filesystem::path>& pathsToCheck, std::wstring& error) const
	{
		return validate(pathsToCheck, true, error);
	}

	bool Batch::completed(const std::vector<std::filesystem::path>& targets, std::wstring& error)
	{
		return completed(targets, targets, error);
	}

	bool Batch::completed(const std::vector<std::filesystem::path>& targets,
		const std::vector<std::filesystem::path>& mutatedPaths, std::wstring& error)
	{
		try
		{
			if (!state_->started || state_->finished) fail("No prepared undo batch exists.");
			std::vector<size_t> group;
			std::vector<size_t> mutated;
			const auto index_for = [&](const std::filesystem::path& target)
			{
				const auto found = state_->lookup.find(absolute_path(target));
				if (found == state_->lookup.end()) fail("A completed path was not protected before mutation.");
				if (std::ranges::find(group, found->second) == group.end()) group.push_back(found->second);
				return found->second;
			};
			for (const auto& target : targets) index_for(target);
			for (const auto& target : mutatedPaths)
			{
				const auto index = index_for(target);
				if (std::ranges::find(mutated, index) == mutated.end()) mutated.push_back(index);
			}
			if (group.size() > 1 && std::ranges::any_of(group,
				[&](const size_t index) { return state_->entry->slots[index].directoryOnly; }))
				fail("Directory nodes must be completed separately from file bundles.");
			// A staged source is a connectivity path, not necessarily a pathname changed by this
			// row. Refreshing it would bless an external edit to a previous row's output.
			for (const auto index : mutated)
			{
				auto& slot = state_->entry->slots[index];
				slot.after = fingerprint(slot.path, !slot.directoryOnly);
				if (slot.directoryOnly && slot.before.front().snapshot.exists == slot.after.front().snapshot.exists)
					fail("Directory-node undo records only creation or removal, not a merged tree.");
				slot.parent = slot.path.parent_path();
				auto parent = files::snapshot_file(slot.parent);
				while (parent && !parent->exists && slot.parent.parent_path() != slot.parent)
				{
					slot.parent = slot.parent.parent_path();
					parent = files::snapshot_file(slot.parent);
				}
				if (!parent || !parent->exists || !parent->directory) fail("Cannot verify a completed file's parent folder.");
				slot.parentState = *parent;
			}
			std::wstring record = L"Bundle completed; current-session Undo candidate.\r\n";
			for (const auto index : group)
				record += (std::ranges::find(mutated, index) == mutated.end() ?
					L"Preserved prior expected state\r\n" : L"Recorded mutation\r\n") +
					slot_text(state_->entry->slots[index]) + state_text(state_->entry->slots[index].after);
			if (!group.empty()) state_->entry->groups.push_back(std::move(group));
			write_record(state_->entry->folder, record);
			return true;
		}
		catch (const std::exception& failure)
		{
			error = L"The operation completed, but Undo recording failed: " + explain(failure) +
				L" Recovery copies remain in " + state_->entry->folder.wstring();
			return false;
		}
	}

	bool Batch::finish(std::wstring& error)
	{
		if (state_->finished) return true;
		state_->finished = true;
		if (!state_->started) return true;
		std::lock_guard lock(state_->history->mutex);
		state_->history->active = false;
		auto& entry = *state_->entry;
		try
		{
			// Connected components keep rename cycles and sidecar bundles indivisible.
			for (size_t i = 0; i < entry.groups.size(); ++i)
				for (size_t j = i + 1; j < entry.groups.size();)
				{
					const bool shared = std::ranges::any_of(entry.groups[j], [&](const size_t index)
					{ return std::ranges::find(entry.groups[i], index) != entry.groups[i].end(); });
					if (!shared) { ++j; continue; }
					for (const auto index : entry.groups[j])
						if (std::ranges::find(entry.groups[i], index) == entry.groups[i].end())
							entry.groups[i].push_back(index);
					entry.groups.erase(entry.groups.begin() + static_cast<std::ptrdiff_t>(j));
					j = i + 1;
				}
			std::erase_if(entry.groups, [&](const auto& group)
			{
				return std::ranges::all_of(group, [&](const size_t index)
				{ return entry.slots[index].before == entry.slots[index].after; });
			});
			for (auto& slot : entry.slots) slot.restored = slot.before == slot.after;
			const auto phase = [&](const auto& group)
			{
				const auto& slot = entry.slots[group.front()];
				return slot.directoryOnly ? (slot.before.front().snapshot.exists ? 0 : 2) : 1;
			};
			std::stable_sort(entry.groups.begin(), entry.groups.end(), [&](const auto& a, const auto& b)
			{
				const auto first = phase(a), second = phase(b);
				if (first != second) return first < second;
				if (first == 1) return false;
				const auto left = entry.slots[a.front()].path.native().size();
				const auto right = entry.slots[b.front()].path.native().size();
				return first == 0 ? left < right : left > right;
			});
			if (!entry.groups.empty()) state_->history->entries.push_back(state_->entry);
			write_record(entry.folder, L"Batch finished; failed/unreached bundles are not eligible for Undo.\r\n");
			return true;
		}
		catch (const std::exception& failure)
		{
			error = L"Finishing undo history failed: " + explain(failure) +
				L" Recovery copies remain in " + entry.folder.wstring();
			return false;
		}
	}

	TransferResult transfer_file(Batch& batch, const std::filesystem::path& source,
		const std::filesystem::path& destination, const bool move, const bool overwrite,
		const std::function<bool()>& beforeCommit, const std::function<bool()>& beforeSourceDelete)
	{
		TransferResult result;
		try
		{
			const std::vector<std::filesystem::path> protectedPaths = move ?
				std::vector<std::filesystem::path>{source, destination} :
				std::vector<std::filesystem::path>{destination};
			if (!batch.protect(protectedPaths, result.detail))
			{
				result.error = std::make_error_code(std::errc::operation_not_permitted);
				return result;
			}
			const auto original = files::snapshot_file(source);
			const auto sourceParent = files::snapshot_file(source.parent_path());
			const auto destinationParent = files::snapshot_file(destination.parent_path());
			if (!original || !original->exists || original->directory || !sourceParent || !destinationParent ||
				!sourceParent->exists || !destinationParent->exists)
				fail("A transfer file or parent cannot be inspected.");
			const bool copyThenDelete = move && (beforeSourceDelete || original->volume != destinationParent->volume);
			const auto validate = [&]
			{
				const auto fromParent = files::snapshot_file(source.parent_path());
				const auto toParent = files::snapshot_file(destination.parent_path());
				if (!fromParent || !toParent || fromParent->volume != sourceParent->volume ||
					fromParent->identity != sourceParent->identity || toParent->volume != destinationParent->volume ||
					toParent->identity != destinationParent->identity || !files::matches_snapshot(source, *original))
				{
					result.detail = L"A transfer source or parent changed before commit.";
					return false;
				}
				return (!beforeCommit || beforeCommit()) && batch.validate_before(protectedPaths, result.detail);
			};
			result.error = move && !copyThenDelete ?
				files::move_file_to(source, destination, overwrite, validate) :
				files::copy_file_to(source, destination, overwrite, validate);
			if (result.error) return result;
			result.destinationCompleted = true;
			result.sourceRemoved = move && !copyThenDelete;
			const auto completed = result.sourceRemoved ? protectedPaths :
				std::vector<std::filesystem::path>{destination};
			if (!batch.completed(completed, result.detail))
			{
				result.error = std::make_error_code(std::errc::io_error);
				return result;
			}
			if (copyThenDelete)
			{
				if (beforeSourceDelete && !beforeSourceDelete())
				{
					result.error = std::make_error_code(std::errc::operation_canceled);
					result.detail = L"The destination was copied, but removing the source was refused.";
					return result;
				}
				{
					Handle file(CreateFileW(files::native_path(source).c_str(), GENERIC_READ | DELETE,
						FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
					const auto parent = files::snapshot_file(source.parent_path());
					if (file.value == INVALID_HANDLE_VALUE || !parent || parent->volume != sourceParent->volume ||
						parent->identity != sourceParent->identity || !batch.validate_before({source}, result.detail) ||
						!batch.validate_completed({destination}, result.detail))
						fail("The destination was copied, but the source or completed destination changed before source removal.");
					FILE_DISPOSITION_INFO remove{TRUE};
					if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &remove, sizeof(remove)))
						throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
							"The destination was copied, but the source could not be removed");
				}
				result.sourceRemoved = true;
				if (!batch.completed(protectedPaths, {source}, result.detail))
					result.error = std::make_error_code(std::errc::io_error);
			}
		}
		catch (const std::system_error& failure)
		{
			result.error = failure.code();
			result.detail = explain(failure);
		}
		catch (const std::exception& failure)
		{
			result.error = std::make_error_code(std::errc::io_error);
			result.detail = explain(failure);
		}
		return result;
	}

	bool History::can_undo() const
	{
		std::unique_lock lock(state_->mutex, std::try_to_lock);
		return lock.owns_lock() && !state_->active && !state_->entries.empty();
	}

	std::wstring History::description() const
	{
		std::unique_lock lock(state_->mutex, std::try_to_lock);
		return !lock.owns_lock() || state_->entries.empty() ? L"" : state_->entries.back()->description;
	}

	Report History::undo()
	{
		std::lock_guard lock(state_->mutex);
		Report report;
		if (state_->active || state_->entries.empty())
		{
			report.details.push_back(state_->active ? L"A file operation is still active." : L"No current-session operation can be undone.");
			return report;
		}
		auto& entry = *state_->entries.back();
		for (const auto& group : entry.groups)
		{
			auto& node = entry.slots[group.front()];
			if (node.directoryOnly)
			{
				if (node.restored) continue;
				try
				{
					auto current = fingerprint(node.path, false);
					if (current.front().snapshot.exists && node.after.front().snapshot.exists)
					{
						current.front().snapshot.modified = node.after.front().snapshot.modified;
						current.front().snapshot.size = node.after.front().snapshot.size;
					}
					const auto parent = files::snapshot_file(node.parent);
					if (current != node.after || !parent || !parent->exists || !parent->directory ||
						parent->volume != node.parentState.volume || parent->identity != node.parentState.identity)
					{
						++report.conflicts;
						report.details.push_back(L"Directory not restored: its identity or parent changed: " + node.path.wstring());
						continue;
					}
					write_record(entry.folder, L"Undo directory-node intent\r\n" + slot_text(node));
					if (node.before.front().snapshot.exists)
					{
						if (!same_content(node.before, fingerprint(node.backup)))
							fail("The directory recovery record changed.");
						if (!std::filesystem::create_directory(files::native_path(node.path)))
							fail("The source directory name was taken; nothing was overwritten.");
						restore_metadata(node.path, node.before);
						const auto restored = fingerprint(node.path, false).front().snapshot;
						const auto rebaseParent = [&](Slot& pending)
						{
							if (!pending.restored && paths::equal(pending.parent, node.path) &&
								pending.parentState.volume == node.before.front().snapshot.volume &&
								pending.parentState.identity == node.before.front().snapshot.identity)
								pending.parentState = restored;
						};
						for (auto& pending : entry.slots) rebaseParent(pending);
						for (size_t previous = 0; previous + 1 < state_->entries.size(); ++previous)
							for (auto& pending : state_->entries[previous]->slots) rebaseParent(pending);
					}
					else
					{
						remove_empty_directory(node.path, node.after.front().snapshot);
					}
					node.restored = true;
					++report.restored;
					report.folders.push_back(node.path.parent_path());
					write_record(entry.folder, L"Directory node restored\r\n" + slot_text(node));
				}
				catch (const std::exception& failure)
				{
					++report.failed;
					report.details.push_back(node.path.wstring() + L": " + explain(failure));
				}
				continue;
			}
			bool conflict = false;
			std::vector<Handle> locks;
			std::vector<Handle> contentLocks;
			std::map<size_t, std::filesystem::path> outputCopies;
			try
			{
				for (const auto index : group)
				{
					const auto& slot = entry.slots[index];
					if (slot.restored) continue;
					const auto parent = files::snapshot_file(slot.parent);
					if (!parent || !parent->exists || !parent->directory ||
						parent->volume != slot.parentState.volume || parent->identity != slot.parentState.identity ||
						fingerprint(slot.path) != slot.after)
					{
						conflict = true;
						report.details.push_back(L"Not restored: the completed file or destination changed: " + slot.path.wstring());
						break;
					}
					if (!slot.backup.empty() && !same_content(slot.before, fingerprint(slot.backup)))
						fail("An original recovery copy changed or is unreadable.");
					if (slot.after.front().snapshot.exists)
					{
						const auto storage = files::snapshot_file(entry.folder);
						if (!storage || !storage->exists) fail("The recovery storage is unavailable.");
						if (storage->volume != slot.after.front().snapshot.volume)
						{
							const auto output = entry.folder / (L"completed-" + unique_id());
							copy_tree(slot.path, output, slot.after);
							outputCopies.emplace(index, output);
						}
						locks.emplace_back(CreateFileW(files::native_path(slot.path).c_str(), GENERIC_READ | DELETE,
							FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
						if (locks.back().value == INVALID_HANDLE_VALUE) fail("Cannot lock a completed file against edits.");
						for (size_t child = 1; child < slot.after.size(); ++child)
						{
							if (slot.after[child].snapshot.directory) continue;
							contentLocks.emplace_back(CreateFileW(
								files::native_path(slot.path / slot.after[child].relative).c_str(), GENERIC_READ,
								FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
								FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
							if (contentLocks.back().value == INVALID_HANDLE_VALUE)
								fail("Cannot lock a completed folder's contents against edits.");
						}
						if (fingerprint(slot.path) != slot.after) fail("A completed file changed while Undo acquired its lock.");
					}
				}
				if (conflict) { ++report.conflicts; continue; }
				size_t held = 0;
				for (const auto index : group)
				{
					auto& slot = entry.slots[index];
					if (slot.restored) continue;
					std::filesystem::path retained;
					if (slot.after.front().snapshot.exists)
					{
						const bool sameVolume = !outputCopies.contains(index);
						const auto output = sameVolume ? entry.folder / (L"completed-" + unique_id()) : outputCopies.at(index);
						retained = sameVolume ? output : slot.path.parent_path() / (L".iw30-undo-" + unique_id());
						write_record(entry.folder, L"Undo intent\r\n" + slot_text(slot) +
							L"Retained completed output: " + output.wstring() +
							L"\r\nRetained staging path: " + retained.wstring() + L"\r\n");
						if (slot.after.front().snapshot.directory)
						{
							// Windows refuses directory renames while descendants are open, even
							// with share-delete. Keep the root identity pin, release our child pins,
							// and verify the entire tree both immediately before and after staging.
							contentLocks.clear();
							if (fingerprint(slot.path) != slot.after)
								fail("A completed folder changed before staging; it was not restored over.");
						}
						rename_locked(locks[held++], retained);
						auto moved = fingerprint(retained);
						moved.front().actualName = slot.after.front().actualName;
						if (moved != slot.after)
						{
							rename_locked(locks[held - 1], slot.path);
							fail("A completed folder changed during Undo; it was returned without restoring over it.");
						}
					}
					try
					{
						if (!slot.backup.empty())
						{
							copy_tree(slot.backup, slot.path, slot.before);
							restore_metadata(slot.path, slot.before);
						}
						slot.restored = true;
						++report.restored;
						report.folders.push_back(slot.path.parent_path());
						const auto restored = fingerprint(slot.path);
						// Copies recreate descendant and parent identities too. Only older states
						// that exactly matched the protected tree may inherit verified new identities.
						for (size_t previous = 0; previous + 1 < state_->entries.size(); ++previous)
							for (auto& earlier : state_->entries[previous]->slots)
								rebase_restored_slot(earlier, slot, restored);
						write_record(entry.folder, L"Restored\r\n" + slot_text(slot) +
							L"Retained completed output: " + retained.wstring() + L"\r\n");
					}
					catch (const std::exception&)
					{
						// A competing creation always wins. Do not remove it or overwrite it in rollback.
						const auto now = files::snapshot_file(slot.path);
						if (!retained.empty() && now && !now->exists)
							rename_locked(locks[held - 1], slot.path);
						throw;
					}
				}
			}
			catch (const std::exception& failure)
			{
				++report.failed;
				report.details.push_back(L"Undo stopped this bundle: " + explain(failure) +
					L" Original and completed-output recovery copies are retained; see " + entry.folder.wstring());
			}
		}
		const bool done = std::ranges::all_of(entry.groups, [&](const auto& group)
		{ return std::ranges::all_of(group, [&](const size_t index) { return entry.slots[index].restored; }); });
		if (done) state_->entries.pop_back();
		return report;
	}

	std::wstring Report::summary() const
	{
		auto text = std::format(L"{} paths restored, {} conflicting bundles, {} failed bundles.", restored, conflicts, failed);
		for (const auto& detail : details) text += L"\r\n" + detail;
		return text;
	}

	void initialize_live_history()
	{
		std::call_once(liveHistoryInitialization, []
		{
			auto value = std::make_unique<History>(platform::module_folder() / L"imagewalker30.undo");
			History* expected = nullptr;
			if (!configuredHistory.compare_exchange_strong(expected, value.get()))
				fail("An explicit undo history is already installed; refusing to initialize live storage.");
			liveHistory = std::move(value);
		});
	}

	History& history()
	{
		auto* value = configuredHistory.load();
		if (!value) fail("Undo history is not initialized. No file operation was started.");
		return *value;
	}

	ScopedHistory::ScopedHistory(History& value) noexcept
		: previous_(configuredHistory.exchange(&value)), installed_(&value)
	{
	}

	ScopedHistory::~ScopedHistory()
	{
		configuredHistory.compare_exchange_strong(installed_, previous_);
	}
}
