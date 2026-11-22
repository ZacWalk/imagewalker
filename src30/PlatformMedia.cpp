// ImageWalker by Zac Walker
// Plays decoded pictures and PCM on libdesktop's renderer and audio device.

#include "PlatformMedia.h"

#include <libdesktop/platform/media.h>
#include <libdesktop/ui/shell.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace iw::media
{
	namespace
	{
		using SteadyClock = std::chrono::steady_clock;
		using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, ticksPerSecond>>;

		// A ring the worker refills between its turns, short enough that a pause is heard at once.
		constexpr double ringSeconds = 0.5;
		// Sound decoded ahead of the ring, and the sound queued before the output starts, so it does not run dry
		// as it starts.
		constexpr std::int64_t soundAhead = ticksPerSecond / 4;
		constexpr std::int64_t soundPrimed = ticksPerSecond / 5;
		// Silence put before sound that starts after the picture, so the clock keeps to media time.
		constexpr std::int64_t longestLeadIn = ticksPerSecond * 5;
		constexpr size_t framesAhead = 3;
		constexpr size_t pooledPictures = 8;
		constexpr auto turn = std::chrono::milliseconds(10);
		constexpr auto pausedTurn = std::chrono::milliseconds(250);
		constexpr auto statusInterval = std::chrono::milliseconds(100);

		std::optional<AudioFormat> pcm_format(const ::platform::audio_output_format& output)
		{
			AudioFormat format;
			format.sampleRate = output.sample_rate;
			format.channels = output.channels;
			format.channelMask = output.channel_mask;
			switch (output.sample_format)
			{
			case ::platform::audio_sample_format::float32: format.format = SampleFormat::float32; break;
			case ::platform::audio_sample_format::int16: format.format = SampleFormat::int16; break;
			case ::platform::audio_sample_format::int32: format.format = SampleFormat::int32; break;
			default: return std::nullopt;
			}
			if (!format.sampleRate || !format.channels) return std::nullopt;
			return format;
		}

		class ReaderPlayer;

		// The player a worker reports to, until that player closes or opens another file.
		struct Link
		{
			ReaderPlayer* player{};
		};

		// One opened file and the worker decoding it. The UI thread sends it commands and takes its pictures; the
		// worker owns the decoder and the audio output, and reports back through queue_ui.
		class Session
		{
		public:
			Session(std::filesystem::path path, std::weak_ptr<Link> link, const float volume, const bool autoplay) :
				path_(std::move(path)), link_(std::move(link)), autoplay_(autoplay)
			{
				commands_.volume = volume;
				thread_ = std::thread([this] { run(); });
			}

			~Session()
			{
				stop_.request_stop();
				command([](Commands& commands) { commands.quit = true; });
				if (thread_.joinable()) thread_.join();
			}

			Session(const Session&) = delete;
			Session& operator=(const Session&) = delete;

			void play()
			{
				command([](Commands& commands) { commands.play = true; });
			}
			void pause()
			{
				command([](Commands& commands) { commands.play = false; });
			}

			void stop()
			{
				command([](Commands& commands)
				{
					commands.stop = true;
					commands.play = false;
					commands.seek.reset();
				});
			}

			void seek(const std::int64_t time)
			{
				command([time](Commands& commands) { commands.seek = time; });
			}
			void volume(const float value)
			{
				command([value](Commands& commands) { commands.volume = value; });
			}

			// The picture the worker last presented, once for each report of one.
			::core::const_surface_ptr take_picture()
			{
				picturePosted_ = false;
				const std::scoped_lock lock(pictureMutex_);
				return std::exchange(picture_, {});
			}

		private:
			struct Commands
			{
				bool quit{};
				bool stop{};
				std::optional<bool> play;
				std::optional<std::int64_t> seek;
				std::optional<float> volume;
			};

			// The worker's own state.
			struct Playback
			{
				explicit Playback(Decoder& source) : decoder(source) {}

				Decoder& decoder;
				::platform::audio_output_ptr output;
				AudioFormat format;
				PlaybackStatus status;
				bool playing{};
				// The media time the clock counts from: the first sound queued since the output was reset, or the
				// picture's position when there is no sound.
				std::int64_t base{};
				SteadyClock::time_point started{};
				bool outputRunning{};
				bool soundQueued{};
				std::uint64_t framesWritten{};
				std::vector<std::uint8_t> pending;
				size_t pendingOffset{};
				// What decodes before a seek's target is passed over.
				std::int64_t skipBefore{};
				bool videoEnded{};
				bool soundEnded{};
				bool needPicture{};
				std::int64_t shownUntil{};
				std::deque<VideoFrame> frames;
				std::optional<VideoFrame> skipped;
				std::vector<::core::surface_ptr> pictures;
				SteadyClock::time_point reported{};
				bool reportNow{};
			};

			template <class Change> void command(Change change)
			{
				{
					const std::scoped_lock lock(mutex_);
					change(commands_);
					pending_ = true;
				}
				wake_.notify_one();
			}

			Commands take_commands(const std::chrono::milliseconds wait)
			{
				std::unique_lock lock(mutex_);
				wake_.wait_for(lock, wait, [this] { return pending_; });
				pending_ = false;
				auto taken = std::exchange(commands_, {});
				commands_.quit = taken.quit;
				return taken;
			}

			template <class Action> void post(Action action) const
			{
				platform::queue_ui([link = link_, action = std::move(action)]
				{
					if (const auto target = link.lock(); target && target->player) action(*target->player);
				});
			}

			void report(const PlaybackStatus& status) const;
			void report_failure(std::wstring error) const;
			void report_picture() const;

			void run()
			{
				const platform::Runtime audioThread(platform::RuntimeMode::multithreaded);
				try
				{
					play_file();
				}
				catch (const std::exception&)
				{
					report_failure(L"Media playback stopped on an unexpected error.");
				}
			}

			void play_file()
			{
				Decoder decoder;
				const auto stop = stop_.get_token();
				const std::stop_callback exit(platform::exit_token(), [this] { stop_.request_stop(); });
				if (!decoder.open(path_, stop))
				{
					if (!stop.stop_requested()) report_failure(decoder.error());
					return;
				}
				Playback p(decoder);
				p.status.notice = decoder.notice();
				if (decoder.has_audio())
				{
					p.output = ::platform::create_audio_output({}, ringSeconds);
					const auto format = p.output ? pcm_format(p.output->format()) : std::nullopt;
					const bool convertible = format && decoder.decode_audio(*format);
					if (!convertible) p.output.reset();
					else p.format = *format;
					if (!p.output)
					{
						p.status
							.notice = !format
						                  ? L"No usable audio output. Check that speakers or headphones are enabled."
						                  : L"The sound cannot be converted to the audio output's format.";
						if (!decoder.has_video())
						{
							report_failure(p.status.notice);
							return;
						}
						platform::write_diagnostic(p.status.notice);
					}
				}
				p.status.hasVideo = decoder.has_video();
				p.status.canSeek = decoder.can_seek();
				if (const auto duration = decoder.duration())
				{
					p.status.duration = *duration;
					p.status.hasDuration = true;
				}
				p.videoEnded = !p.status.hasVideo;
				p.soundEnded = !p.output;
				p.needPicture = p.status.hasVideo;
				p.status.state = PlaybackState::ready;
				report(p.status);
				if (autoplay_)
				{
					set_playing(p, true);
					p.status.state = PlaybackState::playing;
					p.reportNow = true;
				}
				for (;;)
				{
					const auto commands = take_commands(wait_time(p));
					if (commands.quit || stop.stop_requested()) break;
					if (!apply(p, commands)) break;
					if (!decode(p)) break;
					if (!feed_output(p)) break;
					present(p);
					if (finished(p)) finish(p);
					report_progress(p);
				}
				if (p.output) p.output->stop();
			}

			std::chrono::milliseconds wait_time(const Playback& p) const
			{
				if (!p.playing) return p.needPicture ? std::chrono::milliseconds{0} : pausedTurn;
				if (p.needPicture || p.frames.empty()) return turn;
				const auto due = std::chrono::duration_cast<std::chrono::milliseconds>(
					Ticks(p.frames.front().time - clock_now(p)));
				return std::clamp(due, std::chrono::milliseconds{0}, turn);
			}

			// Where playback is, in media time: the audio output's clock while there is sound, otherwise a steady one.
			std::int64_t clock_now(const Playback& p) const
			{
				if (p.output && (!p.soundEnded || p.soundQueued))
					return p.base + static_cast<std::int64_t>(std::llround(p.output->time() * ticksPerSecond));
				if (!p.playing) return p.base;
				return p.base + std::chrono::duration_cast<Ticks>(SteadyClock::now() - p.started).count();
			}

			void set_playing(Playback& p, const bool playing) const
			{
				if (p.playing == playing) return;
				if (playing) p.started = SteadyClock::now();
				else
				{
					if (!p.output || (p.soundEnded && !p.soundQueued)) p.base = clock_now(p);
					if (p.output) p.output->stop();
					p.outputRunning = false;
				}
				p.playing = playing;
			}

			bool apply(Playback& p, const Commands& commands)
			{
				if (commands.volume && p.output) p.output->volume(std::clamp(*commands.volume, 0.0f, 1.0f));
				if (commands.stop)
				{
					set_playing(p, false);
					if (!p.status.canSeek || !seek_to(p, 0))
					{
						report_failure(L"Cannot rewind this file. Reopen it to start again.");
						return false;
					}
					p.status.state = PlaybackState::stopped;
				}
				if (commands.seek && p.status.canSeek)
				{
					if (!seek_to(p, *commands.seek))
					{
						p.status.notice = L"Could not seek to that position.";
						p.reportNow = true;
					}
					else if (p.status.hasDuration && p.status.position == p.status.duration)
					{
						set_playing(p, false);
						p.status.state = PlaybackState::ended;
					}
					else if (p.status.state == PlaybackState::ended) p.status.state = PlaybackState::paused;
				}
				if (!commands.play) return true;
				if (*commands.play)
				{
					if (p.status.state == PlaybackState::playing) return true;
					if (p.status.state == PlaybackState::ended)
					{
						if (!p.status.canSeek || !seek_to(p, 0))
						{
							report_failure(L"Could not restart this file.");
							return false;
						}
					}
					set_playing(p, true);
					p.status.state = PlaybackState::playing;
				}
				else if (p.status.state == PlaybackState::playing)
				{
					set_playing(p, false);
					p.status.state = PlaybackState::paused;
				}
				p.reportNow = true;
				return true;
			}

			bool seek_to(Playback& p, std::int64_t time) const
			{
				time = (std::max)(std::int64_t{0}, p.status.hasDuration ? (std::min)(time, p.status.duration) : time);
				if (!p.decoder.seek(time)) return false;
				p.frames.clear();
				p.skipped.reset();
				p.pending.clear();
				p.pendingOffset = 0;
				if (p.output) p.output->reset();
				p.outputRunning = false;
				p.soundQueued = false;
				p.framesWritten = 0;
				p.base = time;
				p.started = SteadyClock::now();
				p.skipBefore = time;
				p.videoEnded = !p.status.hasVideo;
				p.soundEnded = !p.output;
				p.needPicture = p.status.hasVideo;
				p.shownUntil = time;
				p.status.position = time;
				p.reportNow = true;
				return true;
			}

			::core::surface_ptr free_picture(Playback& p) const
			{
				// A pooled picture nothing else holds - not queued, presented or drawn - is written over, once the
				// fence orders the UI thread's last reads of it before the writes.
				for (const auto& picture : p.pictures)
				{
					if (picture.use_count() != 1) continue;
					std::atomic_thread_fence(std::memory_order_acquire);
					return picture;
				}
				auto picture = std::make_shared<::core::surface>();
				if (p.pictures.size() < pooledPictures) p.pictures.push_back(picture);
				return picture;
			}

			bool decode(Playback& p)
			{
				if (!p.playing && !p.needPicture) return true;
				for (int reads = 0; reads < 8 && !p.videoEnded && p.frames.size() < (p.playing ? framesAhead : 1);
				     ++reads)
				{
					VideoFrame frame;
					frame.picture = free_picture(p);
					const auto read = p.decoder.read_video(frame);
					if (read == ReadResult::failed)
					{
						if (!p.output)
						{
							report_failure(p.decoder.error());
							return false;
						}
						p.status.notice = p.decoder.error() + L". Continuing with sound.";
						p.reportNow = p.videoEnded = true;
						p.needPicture = false;
						break;
					}
					if (read == ReadResult::end)
					{
						p.videoEnded = true;
						// A seek past the last frame still shows it; with no frame at all, the picture stays.
						if (p.frames.empty() && p.skipped) p.frames.push_back(std::move(*p.skipped));
						if (p.frames.empty()) p.needPicture = false;
						p.skipped.reset();
						break;
					}
					if (frame.time + (std::max)(frame.duration, std::int64_t{1}) <= p.skipBefore)
					{
						p.skipped = std::move(frame);
						continue;
					}
					p.skipped.reset();
					p.frames.push_back(std::move(frame));
				}
				if (!p.output || !p.playing) return true;
				for (int reads = 0; reads < 16 && !p.soundEnded && pending_ticks(p) < soundAhead; ++reads)
				{
					AudioBlock block;
					const auto read = p.decoder.read_audio(block);
					if (read == ReadResult::failed)
					{
						return lose_sound(p, p.decoder.error());
					}
					if (read == ReadResult::end) p.soundEnded = true;
					else queue_sound(p, block);
				}
				return true;
			}

			static std::int64_t pending_ticks(const Playback& p)
			{
				const auto frames = (p.pending.size() - p.pendingOffset) / p.format.frame_bytes();
				return static_cast<std::int64_t>(frames) * ticksPerSecond / p.format.sampleRate;
			}

			void queue_sound(Playback& p, const AudioBlock& block) const
			{
				const auto frameBytes = p.format.frame_bytes();
				const auto rate = static_cast<std::int64_t>(p.format.sampleRate);
				const auto frames = static_cast<std::int64_t>(block.samples.size() / frameBytes);
				auto time = block.time;
				std::int64_t skip = 0;
				// Sound from before a seek's target is cut away, to the frame.
				if (time < p.skipBefore) skip = (std::min)(frames, (p.skipBefore - time) * rate / ticksPerSecond);
				if (skip >= frames) return;
				time += skip * ticksPerSecond / rate;
				if (p.pendingOffset)
				{
					p.pending.erase(p.pending.begin(),
					                p.pending.begin() + static_cast<std::ptrdiff_t>(p.pendingOffset));
					p.pendingOffset = 0;
				}
				if (!p.soundQueued)
				{
					p.soundQueued = true;
					const auto gap = time - p.base;
					if (gap > 0)
						p.pending.resize(p.pending.size() +
						                 static_cast<size_t>((std::min)(gap, longestLeadIn) * rate / ticksPerSecond) *
						                     frameBytes);
					else p.base = time;
				}
				const auto first = block.samples.begin() + static_cast<std::ptrdiff_t>(skip * frameBytes);
				p.pending.insert(p.pending.end(), first,
				                 first + static_cast<std::ptrdiff_t>((frames - skip) * frameBytes));
			}

			bool lose_sound(Playback& p, std::wstring reason)
			{
				if (!p.status.hasVideo)
				{
					report_failure(std::move(reason));
					return false;
				}
				p.status.notice = std::move(reason) + L" Continuing without sound.";
				platform::write_diagnostic(p.status.notice);
				p.base = clock_now(p);
				p.started = SteadyClock::now();
				p.output.reset();
				p.outputRunning = false;
				p.pending.clear();
				p.pendingOffset = 0;
				p.soundEnded = p.reportNow = true;
				return true;
			}

			bool feed_output(Playback& p)
			{
				if (!p.output) return true;
				if (p.output->is_device_lost()) return lose_sound(p, L"The audio output was disconnected.");
				if (!p.playing) return true;
				if (p.soundEnded && !p.soundQueued)
				{
					if (p.status.hasVideo || p.skipBefore > 0) return true;
					return lose_sound(p, L"No audio samples could be decoded.");
				}
				const auto frameBytes = p.format.frame_bytes();
				if (p.pendingOffset < p.pending.size())
				{
					const auto taken = p.output->write(
						{p.pending.data() + p.pendingOffset, p.pending.size() - p.pendingOffset});
					p.pendingOffset += taken;
					p.framesWritten += taken / frameBytes;
					if (p.pendingOffset == p.pending.size())
					{
						p.pending.clear();
						p.pendingOffset = 0;
					}
				}
				else if (p.soundEnded && p.outputRunning)
				{
					// An output that runs dry plays its ring again; silence carries the clock on to the picture's end.
					p.output->write_silence();
				}
				if (p.outputRunning) return true;
				const auto written = static_cast<std::int64_t>(p.framesWritten) * ticksPerSecond / p.format.sampleRate;
				if (written >= soundPrimed || (p.soundEnded && p.pendingOffset == p.pending.size()))
				{
					p.output->start();
					if (p.output->is_stopped()) return lose_sound(p, L"The audio output could not start.");
					p.outputRunning = true;
				}
				return true;
			}

			void present(Playback& p)
			{
				if (p.frames.empty()) return;
				if (!p.needPicture)
				{
					if (!p.playing) return;
					const auto now = clock_now(p);
					// A picture whose successor is already due is dropped rather than shown late.
					while (p.frames.size() > 1 && p.frames[1].time <= now) p.frames.pop_front();
					if (p.frames.front().time > now) return;
				}
				p.needPicture = false;
				auto frame = std::move(p.frames.front());
				p.frames.pop_front();
				p.shownUntil = frame.time + (std::max)(frame.duration, std::int64_t{0});
				{
					const std::scoped_lock lock(pictureMutex_);
					picture_ = std::move(frame.picture);
				}
				if (!picturePosted_.exchange(true)) report_picture();
			}

			// Done once the last picture has had its time and the output has played all the sound it was given.
			bool finished(const Playback& p) const
			{
				if (!p.playing || !p.videoEnded || !p.frames.empty()) return false;
				const auto now = clock_now(p);
				if (now < p.shownUntil) return false;
				return !p.output || (p.soundEnded && !p.soundQueued) ||
				       (p.soundEnded && p.pendingOffset == p.pending.size() && p.outputRunning &&
				        now >=
				            p.base + static_cast<std::int64_t>(p.framesWritten) * ticksPerSecond / p.format.sampleRate);
			}

			void finish(Playback& p) const
			{
				set_playing(p, false);
				p.status.state = PlaybackState::ended;
				if (p.status.hasDuration) p.status.position = p.status.duration;
				report(p.status);
				p.reportNow = false;
				p.reported = SteadyClock::now();
			}

			void report_progress(Playback& p) const
			{
				const auto now = SteadyClock::now();
				if (p.playing)
				{
					const auto position = (std::max)(std::int64_t{0}, clock_now(p));
					p.status.position = p.status.hasDuration ? (std::min)(position, p.status.duration) : position;
				}
				if (!p.reportNow && (!p.playing || now - p.reported < statusInterval)) return;
				p.reportNow = false;
				p.reported = now;
				report(p.status);
			}

			const std::filesystem::path path_;
			const std::weak_ptr<Link> link_;
			const bool autoplay_;
			std::mutex mutex_;
			std::condition_variable wake_;
			Commands commands_;
			bool pending_{};
			std::stop_source stop_;
			std::mutex pictureMutex_;
			::core::const_surface_ptr picture_;
			std::atomic_bool picturePosted_{};
			std::thread thread_;
		};

		class ReaderPlayer final : public Player
		{
		public:
			ReaderPlayer(const platform::WindowFramePtr& parent, StatusHandler changed) : changed_(std::move(changed))
			{
				::ui::surface_element::style style;
				style.sampler = render::texture_sampler::bilinear;
				surface_ = std::make_shared<::ui::surface_element>(style);
				surface_->is_visible(false);
				element_ = platform::add_frame_element(parent, surface_);
			}

			~ReaderPlayer() override
			{
				changed_ = {};
				close();
			}

			ReaderPlayer(const ReaderPlayer&) = delete;
			ReaderPlayer& operator=(const ReaderPlayer&) = delete;

			void open(const std::filesystem::path& path, const bool autoplay = true) override
			{
				close();
				path_ = path;
				status_ = {};
				status_.volume = volume_;
				status_.state = PlaybackState::opening;
				notify();
				std::error_code error;
				if (!std::filesystem::is_regular_file(files::native_path(path), error))
				{
					fail(L"The media file is not available.");
					return;
				}
				link_ = std::make_shared<Link>(Link{this});
				session_ = std::make_unique<Session>(path, link_, volume_, autoplay);
			}

			void play() override
			{
				if (status_.state == PlaybackState::stopped && !session_)
				{
					open(path_);
					return;
				}
				if (session_ && can_play(status_.state)) session_->play();
			}

			void pause() override
			{
				if (session_ && status_.state == PlaybackState::playing) session_->pause();
			}

			void stop() override
			{
				if (status_.state == PlaybackState::opening)
				{
					close();
					status_.state = PlaybackState::stopped;
					notify();
					return;
				}
				if (session_ && status_.state != PlaybackState::stopped) session_->stop();
			}

			void seek(const std::int64_t ticks) override
			{
				if (!session_ || !status_.canSeek || !status_.hasDuration || status_.state == PlaybackState::opening ||
				    status_.state == PlaybackState::error)
					return;
				session_->seek(std::clamp(ticks, std::int64_t{0}, status_.duration));
			}

			void volume(const float value) override
			{
				if (!std::isfinite(value)) return;
				volume_ = std::clamp(value, 0.0f, 1.0f);
				status_.volume = volume_;
				if (session_) session_->volume(volume_);
				notify();
			}

			void close() override
			{
				shutdown();
				status_.state = PlaybackState::closed;
				status_.position = 0;
				status_.canSeek = false;
				status_.hasVideo = false;
			}

			void turn(const int clockwiseQuarterTurns) override
			{
				turn_ = clockwiseQuarterTurns;
				present_picture();
			}

			void bounds(const app::recti bounds) override
			{
				if (element_) element_->set_bounds(bounds);
			}

			const PlaybackStatus& status() const override { return status_; }

			::core::const_surface_ptr picture() const override { return picture_; }

			void reported(const PlaybackStatus& status)
			{
				const auto volume = status_.volume;
				status_ = status;
				status_.volume = volume;
				notify();
			}

			void failed(std::wstring error) { fail(std::move(error)); }

			void show_picture()
			{
				auto picture = session_ ? session_->take_picture() : nullptr;
				if (!picture) return;
				picture_ = std::move(picture);
				present_picture();
			}

		private:
			void present_picture()
			{
				if (!element_ || !picture_) return;
				constexpr ::core::simple_transform turns[]{::core::simple_transform::none,
				                                           ::core::simple_transform::rot_270,
				                                           ::core::simple_transform::rot_180,
				                                           ::core::simple_transform::rot_90};
				const auto shown = turn_ == 0 ? picture_ : picture_->transform(turns[std::clamp(turn_, 0, 3)]);
				if (!shown) return;
				element_->show(true);
				surface_->present(shown, element_->frame());
			}

			void shutdown()
			{
				link_.reset();
				session_.reset();
				picture_.reset();
				if (element_) element_->show(false);
			}

			void fail(std::wstring message)
			{
				shutdown();
				status_.state = PlaybackState::error;
				status_.hasVideo = false;
				status_.canSeek = false;
				status_.error = std::move(message);
				platform::write_diagnostic(status_.error + L"\n");
				notify();
			}

			void notify() const
			{
				if (const auto changed = changed_) changed(status_);
			}

			StatusHandler changed_;
			PlaybackStatus status_;
			std::filesystem::path path_;
			float volume_{1.0f};
			int turn_{};
			std::shared_ptr<::ui::surface_element> surface_;
			platform::FrameElementPtr element_;
			std::shared_ptr<Link> link_;
			std::unique_ptr<Session> session_;
			::core::const_surface_ptr picture_;
		};

		void Session::report(const PlaybackStatus& status) const
		{
			post([status](ReaderPlayer& player) { player.reported(status); });
		}

		void Session::report_failure(std::wstring error) const
		{
			post([error = std::move(error)](ReaderPlayer& player) { player.failed(error); });
		}

		void Session::report_picture() const
		{
			post([](ReaderPlayer& player) { player.show_picture(); });
		}
	}

	std::unique_ptr<Player> create_player(const platform::WindowFramePtr& parent, StatusHandler changed)
	{
		return std::make_unique<ReaderPlayer>(parent, std::move(changed));
	}
}
