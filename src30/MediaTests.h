// ImageWalker by Zac Walker
// Register test_media(reporter) in the existing self-test entry point.

#pragma once

#include "PlatformWin32.h"
#include "Media.h"
#include "PlatformMedia.h"
#include "MediaRuntime.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>

namespace iw::tests
{
	namespace media_fixtures
	{
		using Bytes = std::vector<unsigned char>;

		inline bool has_no_normal_media_imports()
		{
			const auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
			if (!image) return false;
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
			const auto* header = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
			if (header->Signature != IMAGE_NT_SIGNATURE ||
				header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
			const auto size = header->OptionalHeader.SizeOfImage;
			const auto directory = header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!directory.VirtualAddress) return true;
			if (directory.VirtualAddress >= size || directory.Size > size - directory.VirtualAddress) return false;
			const auto* imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image + directory.VirtualAddress);
			for (size_t index = 0; index < directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++index)
			{
				const auto nameRva = imports[index].Name;
				if (!nameRva) return true;
				if (nameRva >= size) return false;
				const auto* name = reinterpret_cast<const char*>(image + nameRva);
				if (!std::memchr(name, '\0', size - nameRva)) return false;
				for (const auto* optional : {"mfplat.dll", "mfreadwrite.dll", "mfplay.dll", "mf.dll"})
					if (_stricmp(name, optional) == 0) return false;
			}
			return false;
		}

		inline void u16(Bytes& bytes, const std::uint16_t value)
		{
			bytes.push_back(static_cast<unsigned char>(value));
			bytes.push_back(static_cast<unsigned char>(value >> 8));
		}

		inline void u32(Bytes& bytes, const std::uint32_t value)
		{
			u16(bytes, static_cast<std::uint16_t>(value));
			u16(bytes, static_cast<std::uint16_t>(value >> 16));
		}

		inline void tag(Bytes& bytes, const std::string_view value)
		{
			bytes.insert(bytes.end(), value.begin(), value.end());
		}

		inline void chunk(Bytes& bytes, const std::string_view type, const Bytes& data)
		{
			tag(bytes, type);
			u32(bytes, static_cast<std::uint32_t>(data.size()));
			bytes.insert(bytes.end(), data.begin(), data.end());
			if (data.size() & 1) bytes.push_back(0);
		}

		inline bool save(const std::filesystem::path& path, const Bytes& bytes)
		{
			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			file.close();
			return !file.fail();
		}

		inline Bytes wave()
		{
			Bytes format;
			u16(format, 1); u16(format, 1);
			u32(format, 8000); u32(format, 16000);
			u16(format, 2); u16(format, 16);
			Bytes contents;
			tag(contents, "WAVE");
			chunk(contents, "fmt ", format);
			chunk(contents, "data", Bytes(16000, 0));
			Bytes file;
			chunk(file, "RIFF", contents);
			return file;
		}

		inline Bytes avi()
		{
			Bytes header;
			for (const auto value : {100000u, 10240u, 0u, 0x10u, 10u, 0u, 1u, 1024u, 16u, 16u,
				0u, 0u, 0u, 0u}) u32(header, value);
			Bytes stream;
			tag(stream, "vids"); tag(stream, "DIB ");
			u32(stream, 0); u16(stream, 0); u16(stream, 0);
			for (const auto value : {0u, 1u, 10u, 0u, 10u, 1024u, 0xffffffffu, 0u}) u32(stream, value);
			u16(stream, 0); u16(stream, 0); u16(stream, 16); u16(stream, 16);
			Bytes format;
			u32(format, 40); u32(format, 16); u32(format, 16);
			u16(format, 1); u16(format, 32);
			for (const auto value : {0u, 1024u, 0u, 0u, 0u, 0u}) u32(format, value);
			Bytes streams;
			tag(streams, "strl");
			chunk(streams, "strh", stream);
			chunk(streams, "strf", format);
			Bytes headers;
			tag(headers, "hdrl");
			chunk(headers, "avih", header);
			chunk(headers, "LIST", streams);
			Bytes frame;
			for (int pixel = 0; pixel < 256; ++pixel) u32(frame, 0x002244aa);
			Bytes movies, index;
			tag(movies, "movi");
			for (std::uint32_t number = 0; number < 10; ++number)
			{
				tag(index, "00db"); u32(index, 0x10);
				u32(index, static_cast<std::uint32_t>(movies.size())); u32(index, 1024);
				chunk(movies, "00db", frame);
			}
			Bytes contents;
			tag(contents, "AVI ");
			chunk(contents, "LIST", headers);
			chunk(contents, "LIST", movies);
			chunk(contents, "idx1", index);
			Bytes file;
			chunk(file, "RIFF", contents);
			return file;
		}

		inline bool wait_until(const std::function<bool()>& ready, const std::chrono::milliseconds timeout)
		{
			const auto end = std::chrono::steady_clock::now() + timeout;
			do
			{
				MSG message{};
				while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
				{
					if (message.message == WM_QUIT)
					{
						PostQuitMessage(static_cast<int>(message.wParam));
						return false;
					}
					TranslateMessage(&message);
					DispatchMessageW(&message);
				}
				if (ready()) return true;
				Sleep(10);
			} while (std::chrono::steady_clock::now() < end);
			return ready();
		}
	}

	template<class Reporter>
	void test_media_unavailable(Reporter& reporter, const std::filesystem::path& root,
		const std::filesystem::path& video, const std::filesystem::path& imagePath)
	{
		media::native::testing::ScopedUnavailableBackend unavailable;
		reporter.check(!media::native::acquire(), L"unavailable-backend injection blocks native API acquisition");
		{
			media::native::testing::ScopedUnavailableBackend nested;
			reporter.check(!media::native::acquire(), L"nested availability overrides remain unavailable");
		}
		reporter.check(!media::native::acquire(), L"nested availability override restores its prior state");
		const auto info = media::probe(video);
		reporter.check(!info.hasVideo && !info.hasAudio && !info.hasDuration &&
			info.error.find(L"Media Feature Pack") != std::wstring::npos,
			L"missing media components produce an actionable explanation, not fabricated metadata");
		reporter.check(media::poster(video, 8, 8).pixels.empty(),
			L"unavailable media poster decoding returns safely without resolving an entry point");
		const auto properties = files::read_metadata(video);
		reporter.check(properties.hasSize && !properties.hasDuration &&
			properties.mediaError.find(L"Media Feature Pack") != std::wstring::npos,
			L"file properties retain filesystem information when media support is absent");

		const files::DecodedImage image{2, 2, {0xff112233, 0xff224466, 0xff335577, 0xff446688}, 2, 2};
		reporter.check(files::save_image(image, imagePath, files::ImageSaveFormat::png),
			L"PNG saving does not depend on optional Windows media components");
		const auto decoded = files::load_image(imagePath);
		reporter.check(decoded.width == 2 && decoded.height == 2 && decoded.pixels == image.pixels,
			L"ordinary PNG images still decode when the media backend is unavailable");
		const auto scanned = files::scan_folder_items(root, {});
		reporter.check(scanned.size() == 4 && std::ranges::none_of(scanned,
			[](const auto& item) { return item.hasDuration; }),
			L"folder browsing survives absent media components without inventing durations");

		platform::WindowOptions options;
		options.className = "ImageWalker30.UnavailableMediaTests";
		options.visible = false;
		options.showCommand = 0;
		const auto window = platform::create_top_level_frame({}, options);
		reporter.check(window != nullptr, L"the unavailable-backend fixture creates a non-media window");
		if (!window) return;
		struct CloseWindow
		{
			platform::WindowFramePtr frame;
			~CloseWindow() { frame->set_reactor({}); frame->close(); }
		} closeWindow{window};
		media::PlaybackStatus latest;
		size_t callbacks{};
		auto player = media::create_player(window, [&](const auto& status) { latest = status; ++callbacks; });
		reporter.check(player != nullptr, L"the playback UI can exist without loading optional media DLLs");
		if (!player) return;
		player->open(video);
		reporter.check(latest.state == media::PlaybackState::error &&
			latest.error.find(L"Media Feature Pack") != std::wstring::npos && !latest.hasVideo,
			L"playback reports missing OS support visibly rather than failing process startup");
		player->play();
		player->pause();
		player->stop();
		player->volume(0.5f);
		player->close();
		const auto closedCallbacks = callbacks;
		media_fixtures::wait_until([] { return false; }, std::chrono::milliseconds(150));
		reporter.check(player->status().state == media::PlaybackState::closed && callbacks == closedCallbacks,
			L"unavailable transport commands and shutdown never call unresolved functions or post stale events");
	}

	template<class Reporter>
	void test_media(Reporter& reporter)
	{
		reporter.section(L"Native media metadata, posters, duration sorting and playback cancellation");
		reporter.check(media_fixtures::has_no_normal_media_imports(),
			L"the executable has no normal imports from optional Windows Media Foundation DLLs");
		reporter.check(media::duration_text(-1) == L"0:00" &&
			media::duration_text(3661 * media::ticksPerSecond) == L"1:01:01", L"duration formatting clamps negatives");
		reporter.check(media::frame_rate(30000, 1001) > 29.97 && media::frame_rate(30000, 1001) < 29.98 &&
			media::frame_rate(25, 0) == 0 && media::frame_rate(0xffffffffu, 1) == 0,
			L"frame rates reject zero denominators and implausible malformed ratios");
		reporter.check(media::seek_position(-1, 100) == 0 && media::seek_position(1001, 100) == 100 &&
			media::seek_position(500, (std::numeric_limits<std::int64_t>::max)()) ==
				(std::numeric_limits<std::int64_t>::max)() / 2,
			L"seek arithmetic remains bounded without signed overflow");
		reporter.check(!media::can_play(media::PlaybackState::closed) &&
			!media::can_play(media::PlaybackState::opening) && !media::can_play(media::PlaybackState::error) &&
			media::can_play(media::PlaybackState::paused) && media::can_play(media::PlaybackState::ended),
			L"transport availability reflects completed lifecycle transitions");

		GUID id{};
		const HRESULT guidResult = CoCreateGuid(&id);
		wchar_t unique[40]{};
		StringFromGUID2(id, unique, static_cast<int>(std::size(unique)));
		const auto root = std::filesystem::current_path() / (std::wstring(L"iw30-media-tests-") + unique);
		std::error_code error;
		const bool owned = SUCCEEDED(guidResult) && std::filesystem::create_directory(root, error);
		reporter.check(owned && !error, L"media tests own a unique folder beneath the working directory");
		if (!owned || error) return;
		const auto audio = root / L"tone.wav";
		const auto video = root / L"poster.avi";
		const auto broken = root / L"broken.mp4";
		const auto still = root / L"still.png";
		struct Cleanup
		{
			std::filesystem::path audio, video, broken, still, root;
			~Cleanup()
			{
				std::error_code error;
				for (const auto& path : {audio, video, broken, still, root}) std::filesystem::remove(path, error);
			}
		} cleanup{audio, video, broken, still, root};
		reporter.check(media_fixtures::save(audio, media_fixtures::wave()) &&
			media_fixtures::save(video, media_fixtures::avi()) &&
			media_fixtures::save(broken, {0, 1, 2, 3}), L"native PCM and uncompressed video fixtures are written");
		test_media_unavailable(reporter, root, video, still);
		if (!media::native::acquire())
		{
			platform::write_diagnostic(L"[ SKIP ] Native media fixture decoding requires Windows Media Feature Pack.\n");
			return;
		}
		const auto original = files::snapshot_file(video);
		const auto sound = media::probe(audio);
		reporter.check(sound.error.empty() && sound.hasAudio && !sound.hasVideo && sound.hasDuration &&
			sound.duration == media::ticksPerSecond && sound.codec.find(L"PCM") != std::wstring::npos,
			L"PCM wave metadata reports one-second duration and native codec");
		const auto movie = media::probe(video);
		reporter.check(movie.error.empty() && movie.hasVideo && movie.width == 16 && movie.height == 16 &&
			movie.hasDuration && movie.duration == media::ticksPerSecond && movie.frameRate == 10 &&
			!movie.codec.empty(), L"AVI metadata reports dimensions, duration, frame rate and codec");
		const auto poster = files::load_thumbnail(video, 8, 8);
		reporter.check(poster.width == 8 && poster.height == 8 && poster.originalWidth == 16 &&
			poster.originalHeight == 16 && poster.pixels.size() == 64 &&
			std::ranges::all_of(poster.pixels, [](const auto pixel) { return pixel == 0xff2244aa; }),
			L"video thumbnails contain scaled, opaque decoded poster pixels");
		const auto invalid = media::probe(broken);
		reporter.check(!invalid.error.empty() && !invalid.hasVideo && !invalid.hasAudio &&
			media::poster(broken, 8, 8).pixels.empty(),
			L"malformed media fails explicitly instead of inventing metadata or poster pixels");
		reporter.check(media::probe(audio).hasAudio, L"a malformed media item does not poison the next decode");
		const auto fileInfo = files::read_metadata(video);
		reporter.check(fileInfo.hasDuration && fileInfo.videoWidth == 16 && fileInfo.frameRate == 10,
			L"file properties expose the same native media information");
		std::stop_source cancelled;
		cancelled.request_stop();
		reporter.check(!media::probe(video, cancelled.get_token()).hasVideo &&
			media::poster(video, 8, 8, cancelled.get_token()).pixels.empty(),
			L"cancelled metadata and poster requests do not publish results");
		reporter.check(original && files::matches_snapshot(video, *original),
			L"probing and poster decoding never modify their source file");

		files::FolderItem shortItem, longItem, unknown, folder;
		shortItem.path = L"clip2.wav"; shortItem.kind = files::ItemKind::audio;
		shortItem.hasDuration = true; shortItem.duration = 10;
		longItem = shortItem; longItem.path = L"clip10.wav"; longItem.duration = 20;
		unknown.path = L"unknown.wav"; unknown.kind = files::ItemKind::audio;
		folder.path = L"folder"; folder.kind = files::ItemKind::folder;
		std::vector items{unknown, longItem, folder, shortItem};
		files::sort_items(items, files::SortField::duration);
		reporter.check(items[0].kind == files::ItemKind::folder && items[1].duration == 10 &&
			items[2].duration == 20 && !items[3].hasDuration, L"duration sort keeps folders first and unknowns last");
		files::sort_items(items, files::SortField::duration, false);
		reporter.check(items[0].kind == files::ItemKind::folder && items[1].duration == 20 &&
			items[2].duration == 10 && !items[3].hasDuration, L"descending duration preserves missing-value policy");
		const auto scanned = files::scan_folder_items(root, {});
		reporter.check(scanned.size() == 4 && std::ranges::count(scanned, true,
			&files::FolderItem::hasDuration) == 2, L"folder scanning populates sortable media duration");

		platform::WindowOptions options;
		options.className = "ImageWalker30.MediaTests";
		options.visible = false;
		options.showCommand = 0;
		const auto window = platform::create_top_level_frame({}, options);
		reporter.check(window != nullptr, L"native playback fixture creates a hidden owner window");
		if (!window) return;
		struct CloseWindow
		{
			platform::WindowFramePtr frame;
			~CloseWindow() { frame->set_reactor({}); frame->close(); }
		} closeWindow{window};
		size_t callbacks{};
		auto player = media::create_player(window, [&callbacks](const auto&) { ++callbacks; });
		reporter.check(player != nullptr, L"native playback surface attaches to its owner");
		if (!player) return;
		player->open(broken);
		const bool failed = media_fixtures::wait_until([&]
		{
			return player->status().state == media::PlaybackState::error;
		}, std::chrono::seconds(5));
		reporter.check(failed && !player->status().error.empty(),
			L"asynchronous native playback errors reach the UI state");
		player->open(audio);
		player->stop();
		reporter.check(player->status().state == media::PlaybackState::stopped ||
			player->status().state == media::PlaybackState::error,
			L"Stop cancels a pending open without retaining an opening state");
		player->close();
		const auto closedCallbacks = callbacks;
		media_fixtures::wait_until([] { return false; }, std::chrono::milliseconds(200));
		reporter.check(player->status().state == media::PlaybackState::closed && callbacks == closedCallbacks,
			L"close drops stale callbacks and releases transport resources");
		player->volume(-2.0f);
		reporter.check(player->status().volume == 0.0f, L"negative volume is clamped");
		player->volume(2.0f);
		reporter.check(player->status().volume == 1.0f, L"excess volume is clamped");
		player->open(broken);
		reporter.check(media_fixtures::wait_until([&]
		{
			return player->status().state == media::PlaybackState::error;
		}, std::chrono::seconds(5)), L"the same transport safely reopens after cancellation and shutdown");
		player->close();

		// Rendering needs a desktop/EVR device. Opt in on an interactive Windows test host;
		// metadata, poster pixels, malformed-input and cancellation tests above are unconditional.
		wchar_t integration[2]{};
		if (GetEnvironmentVariableW(L"IW30_TEST_NATIVE_PLAYBACK", integration, 2) == 1 && integration[0] == L'1')
		{
			player->bounds({0, 0, 160, 160});
			player->open(video);
			const bool ready = media_fixtures::wait_until([&]
			{
				return player->status().state == media::PlaybackState::playing ||
					player->status().state == media::PlaybackState::error;
			}, std::chrono::seconds(5));
			const bool playing = ready && player->status().state == media::PlaybackState::playing;
			reporter.check(playing, L"native video fixture reaches actual playback");
			if (playing)
			{
				reporter.check(player->status().canSeek && player->status().hasDuration &&
					player->status().duration >= media::ticksPerSecond,
					L"the native file transport exposes a seekable timeline with a duration");
				player->pause();
				reporter.check(media_fixtures::wait_until([&]
				{
					return player->status().state == media::PlaybackState::paused;
				}, std::chrono::seconds(2)), L"native playback pauses");
				player->seek(media::ticksPerSecond / 2);
				const bool sought = media_fixtures::wait_until([&]
				{
					return player->status().position >= media::ticksPerSecond * 4 / 10;
				}, std::chrono::seconds(2));
				if (!sought)
				{
					const auto& status = player->status();
					platform::write_diagnostic(std::format(
						L"Seek state={} position={} duration={} hasDuration={} canSeek={} error={}\n",
						static_cast<int>(status.state), status.position, status.duration,
						status.hasDuration, status.canSeek, status.error));
				}
				reporter.check(sought, L"native transport seeks while paused");
				player->play();
				reporter.check(media_fixtures::wait_until([&]
				{
					return player->status().state == media::PlaybackState::playing;
				}, std::chrono::seconds(2)), L"native playback resumes");
				player->stop();
				reporter.check(media_fixtures::wait_until([&]
				{
					return player->status().state == media::PlaybackState::stopped &&
						player->status().position == 0;
				}, std::chrono::seconds(2)), L"native playback stops and rewinds");
			}
			else platform::write_diagnostic(player->status().error + L"\n");
			player->close();
			player->volume(0.0f);
			player->open(audio);
			const bool audioReady = media_fixtures::wait_until([&]
			{
				return player->status().state == media::PlaybackState::playing ||
					player->status().state == media::PlaybackState::error;
			}, std::chrono::seconds(5));
			reporter.check(audioReady && player->status().state == media::PlaybackState::playing &&
				!player->status().hasVideo && player->status().hasDuration,
				L"the silent audio fixture reaches actual playback without requiring a video stream");
			if (player->status().state == media::PlaybackState::error)
				platform::write_diagnostic(player->status().error + L"\n");
			player->close();
		}
		else platform::write_diagnostic(
			L"[ SKIP ] EVR rendering/transport integration requires IW30_TEST_NATIVE_PLAYBACK=1 and a desktop.\n");
	}
}
