// ImageWalker by Zac Walker
// MFPlay owns audio/video rendering; its callbacks enqueue owned events for the HWND's UI timer.

#include "PlatformWin32.h"
#include "PlatformMedia.h"
#include "MediaRuntime.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfplay.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <mutex>
#include <optional>

namespace iw::media
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		std::optional<std::int64_t> time_value(const PROPVARIANT& value)
		{
			if (value.vt == VT_I8 && value.hVal.QuadPart >= 0) return value.hVal.QuadPart;
			if (value.vt == VT_UI8 &&
				value.uhVal.QuadPart <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
				return static_cast<std::int64_t>(value.uhVal.QuadPart);
			return {};
		}

		struct Event
		{
			MFP_EVENT_TYPE type{};
			HRESULT result{};
			ComPtr<IMFPMediaItem> item;
		};

		struct Events
		{
			explicit Events(std::shared_ptr<const native::Api> owner) : api(std::move(owner)) {}
			std::shared_ptr<const native::Api> api;
			std::mutex mutex;
			std::deque<Event> pending;
			bool active{true};
			std::atomic_bool failed{};
		};

		class Callback final : public IMFPMediaPlayerCallback
		{
		public:
			explicit Callback(std::shared_ptr<Events> events) : events_(std::move(events)) {}
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override
			{
				if (!value) return E_POINTER;
				*value = nullptr;
				if (iid != __uuidof(IUnknown) && iid != __uuidof(IMFPMediaPlayerCallback)) return E_NOINTERFACE;
				*value = static_cast<IMFPMediaPlayerCallback*>(this);
				AddRef();
				return S_OK;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG remaining = --references_;
				if (!remaining) delete this;
				return remaining;
			}
			void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override
			{
				if (!event) return;
				try
				{
					std::lock_guard lock(events_->mutex);
					if (!events_->active) return;
					if (events_->pending.size() >= 128) { events_->failed = true; return; }
					Event value{event->eEventType, event->hrEvent};
					if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED)
						value.item = reinterpret_cast<MFP_MEDIAITEM_CREATED_EVENT*>(event)->pMediaItem;
					events_->pending.push_back(std::move(value));
				}
				catch (...) { events_->failed = true; }
			}
		private:
			std::atomic<ULONG> references_{1};
			std::shared_ptr<Events> events_;
		};

		class NativePlayer final : public Player
		{
		public:
			explicit NativePlayer(StatusHandler changed) : changed_(std::move(changed)) {}
			~NativePlayer() override
			{
				changed_ = {};
				close();
				if (window_) DestroyWindow(window_);
			}

			bool create(const platform::WindowFramePtr& parent)
			{
				const HWND owner = platform::win32::owner_window(parent);
				if (!owner) return false;
				WNDCLASSW type{};
				type.hInstance = GetModuleHandleW(nullptr);
				type.lpfnWndProc = window_proc;
				type.lpszClassName = L"ImageWalker30.Media";
				type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
				window_ = CreateWindowExW(0, type.lpszClassName, L"Media playback",
					WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, owner, nullptr, type.hInstance, this);
				return window_ != nullptr;
			}

			void open(const std::filesystem::path& path) override
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
					fail(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
					return;
				}
				api_ = native::acquire();
				if (!api_)
				{
					fail(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), std::wstring(native::unavailable_message()));
					return;
				}
				HRESULT result = api_->startup(MF_VERSION, MFSTARTUP_FULL);
				started_ = SUCCEEDED(result);
				if (SUCCEEDED(result))
				{
					events_ = std::make_shared<Events>(api_);
					ComPtr<IMFPMediaPlayerCallback> callback;
					callback.Attach(new Callback(events_));
					result = api_->createPlayer(nullptr, FALSE, MFP_OPTION_FREE_THREADED_CALLBACK,
						callback.Get(), window_, &player_);
				}
				if (SUCCEEDED(result)) result = player_->SetVolume(volume_);
				if (SUCCEEDED(result)) result = api_->createFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST,
					MF_FILEFLAGS_NONE, files::native_path(path).c_str(), &stream_);
				if (SUCCEEDED(result)) result = player_->CreateMediaItemFromObject(stream_.Get(), FALSE, 0, nullptr);
				if (SUCCEEDED(result) && !SetTimer(window_, 1, 100, nullptr))
					result = HRESULT_FROM_WIN32(GetLastError());
				if (FAILED(result)) fail(result);
			}

			void play() override
			{
				if (status_.state == PlaybackState::stopped && !player_) { open(path_); return; }
				if (!player_ || commandPending_ || !can_play(status_.state)) return;
				if (status_.state == PlaybackState::ended && status_.canSeek)
				{
					playAfterSeek_ = true;
					seek(0);
					return;
				}
				commandPending_ = true;
				check(player_->Play());
			}

			void pause() override
			{
				if (player_ && !commandPending_ && status_.state == PlaybackState::playing)
				{
					commandPending_ = true;
					check(player_->Pause());
				}
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
				playAfterSeek_ = false;
				queuedSeek_.reset();
				if (commandPending_) { stopAfterCommand_ = true; return; }
				if (player_ && status_.state != PlaybackState::stopped)
				{
					commandPending_ = true;
					check(player_->Stop());
				}
			}

			void seek(const std::int64_t ticks) override
			{
				if (!player_ || !status_.canSeek || !status_.hasDuration ||
					status_.state == PlaybackState::opening || status_.state == PlaybackState::error) return;
				queuedSeek_ = std::clamp(ticks, std::int64_t{0}, status_.duration);
				if (!seeking_) dispatch_seek();
			}

			void volume(const float value) override
			{
				if (!std::isfinite(value)) return;
				volume_ = std::clamp(value, 0.0f, 1.0f);
				status_.volume = volume_;
				if (player_) check(player_->SetVolume(volume_));
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

			void bounds(const recti bounds) override
			{
				if (!window_) return;
				RECT current{};
				GetWindowRect(window_, &current);
				MapWindowPoints(HWND_DESKTOP, GetParent(window_), reinterpret_cast<POINT*>(&current), 2);
				if (current.left != bounds.x || current.top != bounds.y ||
					current.right - current.left != bounds.width || current.bottom - current.top != bounds.height)
					MoveWindow(window_, bounds.x, bounds.y, (std::max)(0, bounds.width),
						(std::max)(0, bounds.height), TRUE);
			}

			const PlaybackStatus& status() const override { return status_; }

		private:
			void shutdown()
			{
				if (window_) KillTimer(window_, 1);
				if (events_)
				{
					std::deque<Event> discarded;
					{
						std::lock_guard lock(events_->mutex);
						events_->active = false;
						discarded.swap(events_->pending);
					}
				}
				if (player_) player_->Shutdown();
				player_.Reset();
				if (stream_) stream_->Close();
				stream_.Reset();
				events_.reset();
				queuedSeek_.reset();
				seeking_ = playAfterSeek_ = false;
				commandPending_ = stopAfterCommand_ = false;
				if (started_) { api_->shutdown(); started_ = false; }
				api_.reset();
			}

			void fail(const HRESULT result, std::wstring message = {})
			{
				shutdown();
				status_.state = PlaybackState::error;
				status_.hasVideo = false;
				status_.canSeek = false;
				status_.error = message.empty() ? std::format(L"Playback failed (0x{:08X}). Windows needs a supported, "
					L"undamaged file and an installed codec/audio device. No codec is downloaded.",
					static_cast<unsigned long>(result)) : std::move(message);
				platform::write_diagnostic(status_.error + L"\n");
				notify();
			}

			bool check(const HRESULT result)
			{
				if (SUCCEEDED(result)) return true;
				fail(result);
				return false;
			}

			void notify()
			{
				if (window_) InvalidateRect(window_, nullptr, FALSE);
				if (changed_) changed_(status_);
			}

			void dispatch_seek()
			{
				if (!queuedSeek_ || !player_) return;
				PROPVARIANT value{};
				value.vt = VT_I8;
				value.hVal.QuadPart = *queuedSeek_;
				queuedSeek_.reset();
				seeking_ = true;
				check(player_->SetPosition(MFP_POSITIONTYPE_100NS, &value));
			}

			void poll()
			{
				if (!events_ || !player_) return;
				if (events_->failed) { fail(E_OUTOFMEMORY); return; }
				std::deque<Event> pending;
				{
					std::lock_guard lock(events_->mutex);
					pending.swap(events_->pending);
				}
				for (const auto& event : pending)
				{
					if (!check(event.result) || !player_) return;
					switch (event.type)
					{
					case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
						if (!event.item) { fail(MF_E_INVALIDMEDIATYPE); return; }
						{
							BOOL hasVideo{}, selected{};
							event.item->HasVideo(&hasVideo, &selected);
							status_.hasVideo = hasVideo && selected;
							MFP_MEDIAITEM_CHARACTERISTICS characteristics{};
							if (SUCCEEDED(event.item->GetCharacteristics(&characteristics)))
								status_.canSeek = (characteristics & MFP_MEDIAITEM_CAN_SEEK) != 0;
							PROPVARIANT value{};
							if (SUCCEEDED(event.item->GetDuration(MFP_POSITIONTYPE_100NS, &value)))
							{
								if (const auto duration = time_value(value))
								{
									status_.duration = *duration;
									status_.hasDuration = true;
								}
							}
							PropVariantClear(&value);
							if (!check(player_->SetMediaItem(event.item.Get()))) return;
						}
						break;
					case MFP_EVENT_TYPE_MEDIAITEM_SET:
						status_.state = PlaybackState::ready;
						commandPending_ = true;
						if (!check(player_->Play())) return;
						break;
					case MFP_EVENT_TYPE_PLAY:
						commandPending_ = false;
						status_.state = PlaybackState::playing;
						break;
					case MFP_EVENT_TYPE_PAUSE:
						commandPending_ = false;
						status_.state = PlaybackState::paused;
						break;
					case MFP_EVENT_TYPE_STOP:
						commandPending_ = false;
						status_.state = PlaybackState::stopped;
						status_.position = 0;
						seek(0);
						break;
					case MFP_EVENT_TYPE_PLAYBACK_ENDED:
						commandPending_ = false;
						status_.state = PlaybackState::ended;
						if (status_.hasDuration) status_.position = status_.duration;
						break;
					case MFP_EVENT_TYPE_POSITION_SET:
						seeking_ = false;
						if (status_.state == PlaybackState::ended) status_.state = PlaybackState::paused;
						if (queuedSeek_) dispatch_seek();
						else if (playAfterSeek_)
						{
							playAfterSeek_ = false;
							commandPending_ = true;
							if (!check(player_->Play())) return;
						}
						break;
					default: break;
					}
					if (!player_) return;
				}
				if (stopAfterCommand_ && !commandPending_)
				{
					stopAfterCommand_ = false;
					stop();
					if (!player_) return;
				}
				if (!seeking_ && (status_.state == PlaybackState::playing || status_.state == PlaybackState::paused ||
					status_.state == PlaybackState::stopped))
				{
					if (!status_.hasDuration)
					{
						PROPVARIANT duration{};
						if (SUCCEEDED(player_->GetDuration(MFP_POSITIONTYPE_100NS, &duration)))
						{
							if (const auto value = time_value(duration))
							{
								status_.duration = *value;
								status_.hasDuration = true;
							}
						}
						PropVariantClear(&duration);
					}
					PROPVARIANT position{};
					if (SUCCEEDED(player_->GetPosition(MFP_POSITIONTYPE_100NS, &position)))
						if (const auto value = time_value(position)) status_.position = *value;
					PropVariantClear(&position);
				}
				notify();
			}

			static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
			{
				auto* self = reinterpret_cast<NativePlayer*>(GetWindowLongPtrW(window, GWLP_USERDATA));
				if (message == WM_NCCREATE)
				{
					self = static_cast<NativePlayer*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
					self->window_ = window;
					SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
				}
				if (!self) return DefWindowProcW(window, message, wparam, lparam);
				try
				{
					switch (message)
					{
					case WM_TIMER: self->poll(); return 0;
					case WM_SIZE:
						if (self->player_ && self->status_.hasVideo &&
							self->status_.state != PlaybackState::opening)
							self->check(self->player_->UpdateVideo());
						return 0;
					case WM_LBUTTONDOWN:
						SetFocus(GetParent(window));
						return 0;
					case WM_ERASEBKGND: return 1;
					case WM_PAINT:
					{
						PAINTSTRUCT paint{};
						const HDC dc = BeginPaint(window, &paint);
						HRESULT rendered = S_OK;
						RECT bounds{};
						GetClientRect(window, &bounds);
						if (self->player_ && self->status_.hasVideo &&
							self->status_.state != PlaybackState::opening)
							rendered = self->player_->UpdateVideo();
						else
						{
							FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
							SetTextColor(dc, RGB(230, 230, 230));
							SetBkMode(dc, TRANSPARENT);
							const auto text = self->status_.state == PlaybackState::error ? self->status_.error :
								self->status_.state == PlaybackState::opening ? L"Opening media..." :
								self->path_.filename().wstring();
							InflateRect(&bounds, -24, -24);
							DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds,
								DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
						}
						EndPaint(window, &paint);
						if (FAILED(rendered)) self->fail(rendered);
						return 0;
					}
					case WM_DESTROY: self->close(); return 0;
					case WM_NCDESTROY:
						self->window_ = nullptr;
						SetWindowLongPtrW(window, GWLP_USERDATA, 0);
						break;
					default: break;
					}
				}
				catch (...)
				{
					self->shutdown();
					self->status_.state = PlaybackState::error;
					platform::write_diagnostic(L"Media playback failed while processing a window event.\n");
					try
					{
						self->status_.error = L"Media playback failed while processing a window event.";
						self->notify();
					}
					catch (...) {}
				}
				return DefWindowProcW(window, message, wparam, lparam);
			}

			HWND window_{};
			std::shared_ptr<const native::Api> api_;
			ComPtr<IMFPMediaPlayer> player_;
			ComPtr<IMFByteStream> stream_;
			std::shared_ptr<Events> events_;
			StatusHandler changed_;
			PlaybackStatus status_;
			std::filesystem::path path_;
			std::optional<std::int64_t> queuedSeek_;
			float volume_{1.0f};
			bool started_{};
			bool seeking_{};
			bool playAfterSeek_{};
			bool commandPending_{};
			bool stopAfterCommand_{};
		};
	}

	std::unique_ptr<Player> create_player(const platform::WindowFramePtr& parent, StatusHandler changed)
	{
		auto player = std::make_unique<NativePlayer>(std::move(changed));
		if (!player->create(parent)) return {};
		return player;
	}
}
