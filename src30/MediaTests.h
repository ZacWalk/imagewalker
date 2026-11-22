// ImageWalker by Zac Walker
// Register test_media(reporter) in the existing self-test entry point.

#pragma once

#include "MediaEdit.h"

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
			if (header->Signature != IMAGE_NT_SIGNATURE || header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
				return false;
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

		inline Bytes wave(const bool voiced = false)
		{
			Bytes format;
			// A companded tone exercises compressed decode as well as output-rate conversion.
			u16(format, voiced ? 7 : 1);
			u16(format, 1);
			u32(format, 8000);
			u32(format, voiced ? 8000 : 16000);
			u16(format, voiced ? 1 : 2);
			u16(format, voiced ? 8 : 16);
			Bytes contents;
			tag(contents, "WAVE");
			if (voiced) u16(format, 0);
			chunk(contents, "fmt ", format);
			if (voiced)
			{
				Bytes fact;
				u32(fact, 8000);
				chunk(contents, "fact", fact);
				Bytes tone(8000);
				for (size_t i = 0; i < tone.size(); ++i) tone[i] = i % 20 < 10 ? 0xb4 : 0x34;
				chunk(contents, "data", tone);
			}
			else chunk(contents, "data", Bytes(16000, 0));
			Bytes file;
			chunk(file, "RIFF", contents);
			return file;
		}

		// Ten 16 x 16 frames at ten a second, stored bottom up as a DIB is; pixel(frame, x, y) counts y from the top.
		inline Bytes avi(const std::function<std::uint32_t(std::uint32_t, int, int)>& pixel)
		{
			Bytes header;
			for (const auto value : {100000u, 10240u, 0u, 0x10u, 10u, 0u, 1u, 1024u, 16u, 16u, 0u, 0u, 0u, 0u})
				u32(header, value);
			Bytes stream;
			tag(stream, "vids");
			tag(stream, "DIB ");
			u32(stream, 0);
			u16(stream, 0);
			u16(stream, 0);
			for (const auto value : {0u, 1u, 10u, 0u, 10u, 1024u, 0xffffffffu, 0u}) u32(stream, value);
			u16(stream, 0);
			u16(stream, 0);
			u16(stream, 16);
			u16(stream, 16);
			Bytes format;
			u32(format, 40);
			u32(format, 16);
			u32(format, 16);
			u16(format, 1);
			u16(format, 32);
			for (const auto value : {0u, 1024u, 0u, 0u, 0u, 0u}) u32(format, value);
			Bytes streams;
			tag(streams, "strl");
			chunk(streams, "strh", stream);
			chunk(streams, "strf", format);
			Bytes headers;
			tag(headers, "hdrl");
			chunk(headers, "avih", header);
			chunk(headers, "LIST", streams);
			Bytes movies, index;
			tag(movies, "movi");
			for (std::uint32_t number = 0; number < 10; ++number)
			{
				Bytes frame;
				for (int y = 15; y >= 0; --y)
					for (int x = 0; x < 16; ++x) u32(frame, pixel(number, x, y));
				tag(index, "00db");
				u32(index, 0x10);
				u32(index, static_cast<std::uint32_t>(movies.size()));
				u32(index, 1024);
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

		inline Bytes avi()
		{
			return avi([](std::uint32_t, int, int) { return 0x002244aau; });
		}

		// Each frame's blue is its number times 16, and its top half is redder than its bottom half.
		inline std::uint32_t numbered_pixel(const std::uint32_t frame, int, const int y)
		{
			return (y < 8 ? 0x00224400u : 0x00884400u) | frame * 16;
		}

		inline std::uint32_t picture_pixel(const ::core::const_surface_ptr& picture, const int x, const int y)
		{
			std::uint32_t pixel{};
			std::memcpy(&pixel, picture->pixels_line(y) + static_cast<size_t>(x) * 4, sizeof(pixel));
			return pixel & 0x00ffffffu;
		}

		// Waits on the test thread, which stands in for the UI thread: posted UI work runs here.
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
				::platform::pump_ui_tasks();
				if (ready()) return true;
				Sleep(10);
			} while (std::chrono::steady_clock::now() < end);
			return ready();
		}
	}

	template <class Reporter>
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
		reporter.check(info.hasVideo && !info.hasAudio && info.duration == media::ticksPerSecond,
		               L"media metadata works without Media Foundation");
		reporter.check(media::poster(video, 8, 8).pixels.size() == 64,
		               L"the bundled decoder produces video posters without Media Foundation");
		const auto properties = files::read_metadata(video);
		reporter.check(properties.hasSize && properties.hasDuration && properties.mediaError.empty(),
		               L"file properties still report the media duration without Media Foundation");

		const files::DecodedImage image{2, 2, {0xff112233, 0xff224466, 0xff335577, 0xff446688}, 2, 2};
		reporter.check(files::save_image(image, imagePath, files::ImageSaveFormat::png),
		               L"PNG saving does not depend on optional Windows media components");
		const auto decoded = files::load_image(imagePath);
		reporter.check(decoded.width == 2 && decoded.height == 2 && decoded.pixels == image.pixels,
		               L"ordinary PNG images still decode when the media backend is unavailable");
		const auto scanned = files::scan_folder_items(root, {});
		reporter.check(scanned.size() == 4 && std::ranges::count(scanned, true, &files::FolderItem::hasDuration) == 2,
		               L"folder browsing still sorts media durations without Media Foundation");

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
			~CloseWindow()
			{
				frame->set_reactor({});
				frame->close();
			}
		} closeWindow{window};
		media::PlaybackStatus latest;
		size_t callbacks{};
		auto player = media::create_player(window, [&](const auto& status)
		{
			latest = status;
			++callbacks;
		});
		reporter.check(player != nullptr, L"the playback UI can exist without loading optional media DLLs");
		if (!player) return;
		player->open(video);
		reporter.check(media_fixtures::wait_until([&] { return latest.hasVideo && player->picture(); },
		                                          std::chrono::seconds(3)),
		               L"the transport no longer refuses a supported clip when Windows codecs are unavailable");
		player->play();
		player->pause();
		player->stop();
		player->volume(0.5f);
		player->close();
		const auto closedCallbacks = callbacks;
		media_fixtures::wait_until([] { return false; }, std::chrono::milliseconds(150));
		reporter.check(player->status().state == media::PlaybackState::closed && callbacks == closedCallbacks,
		               L"closing bundled playback drops stale reports and releases the session");
	}

	template <class Reporter>
	void test_media_decoder(Reporter& reporter, const std::filesystem::path& frames, const std::filesystem::path& audio,
	                        const std::filesystem::path& broken)
	{
		using media_fixtures::picture_pixel;
		media::Decoder video;
		reporter.check(video.open(frames) && video.has_video() && !video.has_audio() && video.can_seek() &&
		                   video.duration() == media::ticksPerSecond,
		               L"the playback decoder opens a video without a window");
		media::VideoFrame frame;
		const auto first = video.read_video(frame);
		reporter.check(first == media::ReadResult::sample && frame.picture && frame.time == 0 &&
		                   frame.picture->dimensions() == ::core::sizei{16, 16} &&
		                   frame.picture->format() == ::core::texture_format::RGB,
		               L"the first video frame decodes to an opaque 16 x 16 picture at time zero");
		reporter.check(first == media::ReadResult::sample && picture_pixel(frame.picture, 3, 0) == 0x00224400 &&
		                   picture_pixel(frame.picture, 3, 15) == 0x00884400,
		               L"a bottom-up source decodes top down, with its colours intact");
		const auto* const reused = frame.picture.get();
		int count = first == media::ReadResult::sample ? 1 : 0;
		while (video.read_video(frame) == media::ReadResult::sample && count < 20) ++count;
		reporter.check(count == 10 && frame.picture.get() == reused,
		               L"every frame decodes into the picture handed back, and the stream then ends");
		const bool sought = video.seek(media::ticksPerSecond * 55 / 100);
		frame = {};
		reporter.check(sought && video.read_video(frame) == media::ReadResult::sample &&
		                   frame.time == media::ticksPerSecond / 2 &&
		                   picture_pixel(frame.picture, 0, 0) == (0x00224400u | 5 * 16),
		               L"a seek lands on the frame showing at the time asked for");

		media::Decoder sound;
		const media::AudioFormat format{48000, 2, 3, media::SampleFormat::float32};
		reporter.check(sound.open(audio) && sound.has_audio() && !sound.has_video() && sound.decode_audio(format),
		               L"the playback decoder converts mono 8 kHz PCM to a 48 kHz stereo output format");
		std::uint64_t frames48k = 0;
		bool silent = true;
		bool startsAtZero = false;
		media::AudioBlock block;
		for (int blocks = 0; blocks < 1000 && sound.read_audio(block) == media::ReadResult::sample; ++blocks)
		{
			if (blocks == 0) startsAtZero = block.time == 0;
			frames48k += block.samples.size() / format.frame_bytes();
			silent = silent && std::ranges::all_of(block.samples, [](const auto value) { return value == 0; });
		}
		reporter.check(startsAtZero && silent && frames48k >= 47500 && frames48k <= 48500,
		               L"one second of silence resamples to one second of output-rate frames");

		media::Decoder damaged;
		reporter.check(!damaged.open(broken) && !damaged.error().empty(),
		               L"the playback decoder refuses malformed media with a reason");
		{
			media::native::testing::ScopedUnavailableBackend unavailable;
			media::Decoder absent;
			reporter.check(absent.open(frames) && absent.has_video(),
			               L"the playback decoder opens a supported video without Windows media components");
		}
	}

	template <class Reporter>
	void test_media_playback(Reporter& reporter, const platform::WindowFramePtr& window,
	                         const std::filesystem::path& frames, const std::filesystem::path& audio)
	{
		using media_fixtures::picture_pixel;
		using media_fixtures::wait_until;
		size_t callbacks{};
		auto player = media::create_player(window, [&callbacks](const auto&) { ++callbacks; });
		const auto blue = [&player]
		{
			const auto picture = player->picture();
			return picture && picture->dimensions() == ::core::sizei{16, 16}
			           ? static_cast<int>(picture_pixel(picture, 0, 0) & 0xff)
			           : -1;
		};
		player->open(frames);
		const bool playing = wait_until([&] {
			return player->status().state == media::PlaybackState::playing && player->picture();
		}, std::chrono::seconds(5));
		reporter.check(playing && player->status().hasVideo && player->status().canSeek &&
		                   player->status().duration == media::ticksPerSecond,
		               L"video plays without a window, its frames decoded to pictures");
		if (!playing)
		{
			platform::write_diagnostic(player->status().error + L"\n");
			return;
		}
		player->pause();
		reporter.check(wait_until([&] { return player->status().state == media::PlaybackState::paused; },
		                          std::chrono::seconds(2)),
		               L"playback pauses");
		player->seek(media::ticksPerSecond * 55 / 100);
		reporter.check(wait_until([&]
		{ return player->status().position == media::ticksPerSecond * 55 / 100 && blue() == 5 * 16; },
		                          std::chrono::seconds(2)) &&
		                   player->status().state == media::PlaybackState::paused,
		               L"a paused seek shows the frame at its time");
		const auto paused = player->status().position;
		wait_until([] { return false; }, std::chrono::milliseconds(150));
		reporter.check(player->status().position == paused && blue() == 5 * 16, L"a paused clock holds still");
		player->play();
		reporter.check(wait_until([&] { return player->status().state == media::PlaybackState::ended; },
		                          std::chrono::seconds(3)) &&
		                   player->status().position == media::ticksPerSecond && blue() == 9 * 16,
		               L"playback resumes and ends on the last frame at the media's duration");
		player->play();
		reporter.check(wait_until(
						   [&]
		{
			return player->status().state == media::PlaybackState::playing &&
			       player->status().position < media::ticksPerSecond / 2;
		}, std::chrono::seconds(2)),
		               L"Play after the end starts again from the beginning");
		player->stop();
		reporter.check(wait_until(
						   [&]
		{
			return player->status().state == media::PlaybackState::stopped && player->status().position == 0 &&
			       blue() == 0;
		}, std::chrono::seconds(2)),
		               L"Stop rewinds to the first frame");
		player->close();
		const auto closedCallbacks = callbacks;
		wait_until([] { return false; }, std::chrono::milliseconds(150));
		reporter.check(player->status().state == media::PlaybackState::closed && !player->picture() &&
		                   callbacks == closedCallbacks,
		               L"close stops the worker and drops its late reports");

		player->volume(0.0f);
		player->open(audio);
		const bool settled = wait_until([&]
		{
			return player->status().state == media::PlaybackState::playing ||
			       player->status().state == media::PlaybackState::error;
		}, std::chrono::seconds(5));
		if (::platform::list_audio_playback_devices().empty())
		{
			reporter.check(settled && player->status().state == media::PlaybackState::error &&
			                   player->status().error.find(L"audio output") != std::wstring::npos,
			               L"sound with no audio output fails visibly");
			platform::write_diagnostic(L"[ SKIP ] Audio output playback needs an audio endpoint.\n");
		}
		else
		{
			reporter.check(settled && player->status().state == media::PlaybackState::playing &&
			                   !player->status().hasVideo && player->status().hasDuration,
			               L"sound plays through the audio output");
			reporter.check(wait_until([&] { return player->status().state == media::PlaybackState::ended; },
			                          std::chrono::seconds(4)) &&
			                   player->status().position == player->status().duration,
			               L"the audio output's clock carries sound to its end");
		}
		player->close();
	}

	template <class Reporter>
	void test_media_edits(Reporter& reporter, const std::filesystem::path& audio, const std::filesystem::path& video,
	                      const std::filesystem::path& broken, const std::filesystem::path& root)
	{
		reporter.section(L"Packet-copy media editor and guarded publication");
		const auto resultPath = root / L"cut-\u00e9.wav";
		const auto aviCopy = root / L"copied.avi";
		const auto movCopy = root / L"turned.mov";
		const auto cancelledPath = root / L"cancelled.wav";
		const auto original = files::file_digest(audio);
		const auto originalBackup = root / L"cut-\u00e9-original.wav";
		struct Cleanup
		{
			std::array<std::filesystem::path, 5> owned;
			~Cleanup()
			{
				std::error_code error;
				for (const auto& path : owned) std::filesystem::remove(path, error);
			}
		} cleanup{{resultPath, aviCopy, movCopy, cancelledPath, originalBackup}};
		const auto info = media::edit::inspect(audio, L".wav");
		reporter.check(info.error.empty() && info.audio && !info.video && info.sameFormat &&
		                   info.duration == media::ticksPerSecond,
		               L"FFmpeg identifies a one-second PCM input without using Windows playback");
		media::edit::Draft trim;
		trim.last = media::ticksPerSecond / 2;
		media::edit::Result copy;
		const auto write = [&](const std::filesystem::path& in, const std::filesystem::path& destination,
		                       const files::FileSnapshot& expected, const media::edit::Draft& draft,
		                       const std::stop_token& stop = {})
		{
			return files::write_staged_file(destination, expected, [&](const files::StagedStream& stream)
			{
				copy = media::edit::copy(in, destination.extension().wstring(), draft, true, stream, stop);
				if (!copy) platform::write_diagnostic(copy.error + L"\n");
				return static_cast<bool>(copy);
			}, [&] { return !stop.stop_requested(); });
		};
		const auto written = write(audio, resultPath, {}, trim);
		const auto trimmed = media::edit::inspect(resultPath, L".wav");
		reporter.check(!written && trimmed.audio && trimmed.duration >= media::ticksPerSecond / 2 &&
		                   trimmed.duration < 3 * media::ticksPerSecond / 4 && original == files::file_digest(audio),
		               L"trim produces a shorter playable PCM copy and leaves source bytes untouched");
		const auto copiedDigest = files::file_digest(resultPath);
		reporter.check(copiedDigest && write(audio, resultPath, {}, {}) &&
		                   files::file_digest(resultPath) == copiedDigest,
		               L"Save As refuses an occupied name without changing its bytes");
		std::stop_source cancelled;
		cancelled.request_stop();
		reporter.check(write(audio, cancelledPath, {}, trim, cancelled.get_token()) &&
		                   !std::filesystem::exists(cancelledPath),
		               L"a cancelled media save publishes no file");
		reporter.check(write(broken, cancelledPath, {}, {}) && !std::filesystem::exists(cancelledPath),
		               L"unreadable media does not publish an apparent successful copy");
		reporter.check(!write(video, aviCopy, {}, {}) && media::edit::inspect(aviCopy, L".avi").video &&
		                   files::file_digest(video).has_value(),
		               L"video packets survive a complete AVI rebuild without an encoder");
		media::edit::Draft turn;
		turn.turn = 1;
		const auto movieWritten = write(video, movCopy, {}, turn);
		std::ifstream movie(movCopy, std::ios::binary);
		const std::string atoms((std::istreambuf_iterator<char>(movie)), {});
		reporter.check(!movieWritten && media::edit::inspect(movCopy, L".mov").video &&
		                   atoms.find("moov") < atoms.find("mdat") && atoms.find("mdat") != std::string::npos,
		               L"the staged movie can be reopened after the index-first rotation copy");
		const auto snapshot = files::snapshot_file(resultPath);
		if (snapshot && snapshot->exists)
		{
			reporter.check(!write(audio, resultPath, *snapshot, {}) &&
			                   files::file_digest(originalBackup) == copiedDigest &&
			                   media::edit::inspect(resultPath, L".wav").duration == info.duration,
			               L"explicit replacement keeps a verified byte-for-byte original beside it");
		}
	}

	template <class Reporter> void test_media(Reporter& reporter)
	{
		reporter.section(L"Media metadata, posters, duration sorting, decoding and headless playback");
		reporter.check(media_fixtures::has_no_normal_media_imports(),
		               L"the executable has no normal imports from optional Windows Media Foundation DLLs");
		reporter.check(media::duration_text(-1) == L"0:00" &&
		                   media::duration_text(3661 * media::ticksPerSecond) == L"1:01:01",
		               L"duration formatting clamps negatives");
		reporter.check(media::frame_rate(30000, 1001) > 29.97 && media::frame_rate(30000, 1001) < 29.98 &&
		                   media::frame_rate(25, 0) == 0 && media::frame_rate(0xffffffffu, 1) == 0,
		               L"frame rates reject zero denominators and implausible malformed ratios");
		reporter.check(media::seek_position(-1, 100) == 0 && media::seek_position(1001, 100) == 100 &&
		                   media::seek_position(500, (std::numeric_limits<std::int64_t>::max)()) ==
		                       (std::numeric_limits<std::int64_t>::max)() / 2,
		               L"seek arithmetic remains bounded without signed overflow");
		reporter.check(!media::can_play(media::PlaybackState::closed) &&
		                   !media::can_play(media::PlaybackState::opening) &&
		                   !media::can_play(media::PlaybackState::error) &&
		                   media::can_play(media::PlaybackState::paused) &&
		                   media::can_play(media::PlaybackState::ended),
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
		const auto frames = root / L"frames.avi";
		const auto tone = root / L"voiced.wav";
		struct Cleanup
		{
			std::filesystem::path audio, video, broken, still, frames, tone, root;
			~Cleanup()
			{
				std::error_code error;
				for (const auto& path : {audio, video, broken, still, frames, tone, root})
					std::filesystem::remove(path, error);
			}
		} cleanup{audio, video, broken, still, frames, tone, root};
		reporter.check(media_fixtures::save(audio, media_fixtures::wave()) &&
		                   media_fixtures::save(video, media_fixtures::avi()) &&
		                   media_fixtures::save(broken, {0, 1, 2, 3}),
		               L"native PCM and uncompressed video fixtures are written");
		test_media_edits(reporter, audio, video, broken, root);
		test_media_unavailable(reporter, root, video, still);
		const auto original = files::snapshot_file(video);
		const auto sound = media::probe(audio);
		reporter.check(sound.error.empty() && sound.hasAudio && !sound.hasVideo && sound.hasDuration &&
		                   sound.duration == media::ticksPerSecond && sound.codec.find(L"PCM") != std::wstring::npos,
		               L"PCM wave metadata reports one-second duration and native codec");
		const auto movie = media::probe(video);
		reporter.check(movie.error.empty() && movie.hasVideo && movie.width == 16 && movie.height == 16 &&
		                   movie.hasDuration && movie.duration == media::ticksPerSecond && movie.frameRate == 10 &&
		                   !movie.codec.empty(),
		               L"AVI metadata reports dimensions, duration, frame rate and codec");
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
		shortItem.path = L"clip2.wav";
		shortItem.kind = files::ItemKind::audio;
		shortItem.hasDuration = true;
		shortItem.duration = 10;
		longItem = shortItem;
		longItem.path = L"clip10.wav";
		longItem.duration = 20;
		unknown.path = L"unknown.wav";
		unknown.kind = files::ItemKind::audio;
		folder.path = L"folder";
		folder.kind = files::ItemKind::folder;
		std::vector items{unknown, longItem, folder, shortItem};
		files::sort_items(items, files::SortField::duration);
		reporter.check(items[0].kind == files::ItemKind::folder && items[1].duration == 10 && items[2].duration == 20 &&
		                   !items[3].hasDuration,
		               L"duration sort keeps folders first and unknowns last");
		files::sort_items(items, files::SortField::duration, false);
		reporter.check(items[0].kind == files::ItemKind::folder && items[1].duration == 20 && items[2].duration == 10 &&
		                   !items[3].hasDuration,
		               L"descending duration preserves missing-value policy");
		const auto scanned = files::scan_folder_items(root, {});
		reporter.check(scanned.size() == 4 && std::ranges::count(scanned, true, &files::FolderItem::hasDuration) == 2,
		               L"folder scanning populates sortable media duration");
		reporter.check(media_fixtures::save(frames, media_fixtures::avi(media_fixtures::numbered_pixel)),
		               L"a video fixture with numbered frames is written");
		test_media_decoder(reporter, frames, audio, broken);
		reporter.check(media_fixtures::save(tone, media_fixtures::wave(true)), L"a voiced G.711 test file is written");
		{
			media::native::testing::ScopedUnavailableBackend unavailable;
			media::Decoder speech;
			const media::AudioFormat format{48000, 2, 3, media::SampleFormat::float32};
			media::AudioBlock block;
			const auto initialized = speech.open(tone) && speech.decode_audio(format);
			reporter.check(initialized && speech.read_audio(block) == media::ReadResult::sample &&
			                   std::ranges::any_of(block.samples, [](const auto value) { return value != 0; }),
			               L"bundled decode converts compressed audio to nonzero interleaved output PCM");
			const bool sought = initialized && speech.seek(media::ticksPerSecond / 2);
			reporter.check(sought && speech.read_audio(block) == media::ReadResult::sample &&
			                   block.time >= media::ticksPerSecond / 2 && block.time < 3 * media::ticksPerSecond / 4,
			               L"seeking compressed audio drops the old resampler state and resumes at the requested time");
		}

		platform::WindowOptions options;
		options.className = "ImageWalker30.MediaTests";
		options.visible = false;
		options.showCommand = 0;
		const auto window = platform::create_top_level_frame({}, options);
		reporter.check(window != nullptr, L"the playback fixture creates a hidden owner window");
		if (!window) return;
		struct CloseWindow
		{
			platform::WindowFramePtr frame;
			~CloseWindow()
			{
				frame->set_reactor({});
				frame->close();
			}
		} closeWindow{window};
		size_t callbacks{};
		auto player = media::create_player(window, [&callbacks](const auto&) { ++callbacks; });
		reporter.check(player != nullptr, L"a player attaches to its owner window");
		if (!player) return;
		player->open(video, false);
		reporter.check(media_fixtures::wait_until([&] { return player->picture() != nullptr; },
		                                          std::chrono::seconds(3)) &&
		                   player->status().state == media::PlaybackState::ready && player->status().position == 0,
		               L"entering an edit previews the first frame without starting playback");
		player->open(broken);
		const bool failed = media_fixtures::wait_until([&]
		{ return player->status().state == media::PlaybackState::error; }, std::chrono::seconds(5));
		reporter.check(failed && !player->status().error.empty(), L"asynchronous playback errors reach the UI state");
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
		reporter.check(media_fixtures::wait_until([&] { return player->status().state == media::PlaybackState::error; },
		                                          std::chrono::seconds(5)),
		               L"the same transport safely reopens after cancellation and shutdown");
		player->close();

		test_media_playback(reporter, window, frames, tone);
	}
}
