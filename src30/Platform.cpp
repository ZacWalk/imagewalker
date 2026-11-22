// ImageWalker by Zac Walker
// Adapts the ImageWalker frame model to libdesktop's platform, render and ui libraries.

#include "Platform.h"
#include "DesktopServices.h"
#include "Files.h"
#include "PlatformWin32.h"
#include "DiagnosticLog.h"
#include "Appearance.h"

#include <core/recycle.h>
#include <iw/imaging/PixelOps.h>

#include <objbase.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <limits>
#include <mutex>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

namespace iw::platform
{
	namespace
	{
		Appearance currentAppearance = Appearance::light;

		core::recti core_rect(const app::recti v)
		{
			return static_cast<core::recti>(v);
		}

		app::recti app_rect(const core::recti v)
		{
			return {v.left, v.top, v.width(), v.height()};
		}

		core::sizei core_size(const app::sizei v)
		{
			return static_cast<core::sizei>(v);
		}

		app::sizei app_size(const core::sizei v)
		{
			return {v.cx, v.cy};
		}

		core::color core_color(const color v)
		{
			return core::color(core::rgba(v.r, v.g, v.b, v.a));
		}

		color app_color(const core::color32 v)
		{
			const auto alpha = core::get_a(v);
			return color::from_packed(v, static_cast<std::uint8_t>(alpha ? alpha : 255));
		}

		std::uint8_t unpack_clipboard_channel(const std::uint32_t value, const std::uint32_t mask)
		{
			if (!mask) return 0;
			unsigned shift = 0;
			while (shift < 32 && ((mask >> shift) & 1u) == 0) ++shift;
			const auto maximum = mask >> shift;
			const auto channel = (value & mask) >> shift;
			return static_cast<std::uint8_t>((static_cast<std::uint64_t>(channel) * 255u + maximum / 2u) / maximum);
		}

		void finish_clipboard_image(BitmapResource& image)
		{
			for (auto& pixel : image.pixels) pixel |= 0xff000000u;
			image.hasAlpha = false;
		}

		std::optional<BitmapResource> read_clipboard_dib(HGLOBAL handle)
		{
			if (!handle) return {};
			const auto total = GlobalSize(handle);
			const auto* const memory = static_cast<const std::uint8_t*>(GlobalLock(handle));
			if (!memory) return {};
			std::optional<BitmapResource> result;
			do
			{
				if (total < sizeof(BITMAPINFOHEADER)) break;
				const auto* const header = reinterpret_cast<const BITMAPINFOHEADER*>(memory);
				if (header->biSize < sizeof(BITMAPINFOHEADER) || header->biSize > total || header->biPlanes != 1 ||
				    header->biWidth <= 0 || header->biWidth > INT_MAX || header->biHeight == 0 ||
				    header->biHeight == std::numeric_limits<LONG>::min())
					break;
				if (header->biCompression != BI_RGB && header->biCompression != BI_BITFIELDS) break;
				const auto bits = static_cast<std::size_t>(header->biBitCount);
				if (bits != 8 && bits != 16 && bits != 24 && bits != 32) break;
				const auto width = static_cast<std::size_t>(header->biWidth);
				const auto height = static_cast<std::size_t>(header->biHeight < 0 ? -header->biHeight
				                                                                  : header->biHeight);
				if (width > std::numeric_limits<std::size_t>::max() / bits) break;
				const auto row_bits = width * bits;
				if (row_bits > std::numeric_limits<std::size_t>::max() - 31) break;
				const auto row_bytes = (row_bits + 31) / 32 * 4;
				std::uint32_t red_mask{}, green_mask{}, blue_mask{};
				std::size_t masks_size{};
				if (header->biCompression == BI_BITFIELDS)
				{
					if (header->biSize >= 52)
					{
						std::memcpy(&red_mask, memory + 40, sizeof(red_mask));
						std::memcpy(&green_mask, memory + 44, sizeof(green_mask));
						std::memcpy(&blue_mask, memory + 48, sizeof(blue_mask));
					}
					else
					{
						masks_size = 3 * sizeof(std::uint32_t);
						if (static_cast<std::size_t>(header->biSize) + masks_size > total) break;
						std::memcpy(&red_mask, memory + header->biSize, sizeof(red_mask));
						std::memcpy(&green_mask, memory + header->biSize + 4, sizeof(green_mask));
						std::memcpy(&blue_mask, memory + header->biSize + 8, sizeof(blue_mask));
					}
				}
				if (bits == 16 && header->biCompression == BI_RGB)
				{
					red_mask = 0x7c00;
					green_mask = 0x03e0;
					blue_mask = 0x001f;
				}
				if (bits == 32 && header->biCompression == BI_RGB)
				{
					red_mask = 0x00ff0000;
					green_mask = 0x0000ff00;
					blue_mask = 0x000000ff;
				}
				const auto palette_count = bits <= 8 ? (header->biClrUsed ? static_cast<std::size_t>(header->biClrUsed)
				                                                          : (std::size_t{1} << bits))
				                                     : std::size_t{0};
				const auto palette_size = palette_count > std::numeric_limits<std::size_t>::max() / sizeof(RGBQUAD)
				                              ? std::numeric_limits<std::size_t>::max()
				                              : palette_count * sizeof(RGBQUAD);
				std::size_t offset = header->biSize;
				if (offset > total || masks_size > total - offset) break;
				offset += masks_size;
				if (palette_size > total - offset) break;
				const auto* const palette = reinterpret_cast<const RGBQUAD*>(memory + offset);
				offset += palette_size;
				if (offset > total || row_bytes > std::numeric_limits<std::size_t>::max() / height ||
				    row_bytes * height > total - offset)
					break;
				BitmapResource image;
				image.size = {static_cast<int>(width), static_cast<int>(height)};
				image.pixels.resize(width * height);
				const auto* const pixels = memory + offset;
				const bool top_down = header->biHeight < 0;
				for (std::size_t y = 0; y < height; ++y)
				{
					const auto* const row = pixels + (top_down ? y : height - y - 1) * row_bytes;
					for (std::size_t x = 0; x < width; ++x)
					{
						std::uint32_t value{};
						if (bits == 8)
						{
							const auto index = row[x];
							if (index >= palette_count) continue;
							const auto& entry = palette[index];
							value = static_cast<std::uint32_t>(entry.rgbRed) << 16 |
							        static_cast<std::uint32_t>(entry.rgbGreen) << 8 | entry.rgbBlue;
						}
						else if (bits == 16)
						{
							std::memcpy(&value, row + x * 2, sizeof(std::uint16_t));
							value = static_cast<std::uint32_t>(unpack_clipboard_channel(value, red_mask)) << 16 |
							        static_cast<std::uint32_t>(unpack_clipboard_channel(value, green_mask)) << 8 |
							        unpack_clipboard_channel(value, blue_mask);
						}
						else if (bits == 24)
						{
							const auto* const source = row + x * 3;
							value = static_cast<std::uint32_t>(source[2]) << 16 |
							        static_cast<std::uint32_t>(source[1]) << 8 | source[0];
						}
						else
						{
							std::memcpy(&value, row + x * 4, sizeof(value));
							value = static_cast<std::uint32_t>(unpack_clipboard_channel(value, red_mask)) << 16 |
							        static_cast<std::uint32_t>(unpack_clipboard_channel(value, green_mask)) << 8 |
							        unpack_clipboard_channel(value, blue_mask);
						}
						image.pixels[y * width + x] = 0xff000000u | value;
					}
				}
				finish_clipboard_image(image);
				result = std::move(image);
			} while (false);
			GlobalUnlock(handle);
			return result;
		}

		std::optional<BitmapResource> read_clipboard_image(HWND owner)
		{
			if (!OpenClipboard(owner)) return {};
			std::optional<BitmapResource> result;
			if (const auto handle = static_cast<HGLOBAL>(GetClipboardData(CF_DIBV5)); handle)
				result = read_clipboard_dib(handle);
			if (!result)
				if (const auto handle = static_cast<HGLOBAL>(GetClipboardData(CF_DIB)); handle)
					result = read_clipboard_dib(handle);
			if (!result)
				if (const auto bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP)); bitmap)
				{
					BITMAP info{};
					if (GetObjectW(bitmap, sizeof(info), &info) == sizeof(info) && info.bmWidth > 0 &&
					    info.bmHeight > 0)
					{
						BitmapResource image;
						image.size = {info.bmWidth, info.bmHeight};
						image.pixels.resize(static_cast<std::size_t>(info.bmWidth) * info.bmHeight);
						BITMAPINFO format{};
						format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
						format.bmiHeader.biWidth = info.bmWidth;
						format.bmiHeader.biHeight = -info.bmHeight;
						format.bmiHeader.biPlanes = 1;
						format.bmiHeader.biBitCount = 32;
						format.bmiHeader.biCompression = BI_RGB;
						if (const auto dc = GetDC(nullptr); dc)
						{
							const auto rows = GetDIBits(dc, bitmap, 0, static_cast<UINT>(info.bmHeight),
							                            image.pixels.data(), &format, DIB_RGB_COLORS);
							ReleaseDC(nullptr, dc);
							if (rows == info.bmHeight)
							{
								finish_clipboard_image(image);
								result = std::move(image);
							}
						}
					}
				}
			CloseClipboard();
			return result;
		}

		HWND hwnd_from_any(const std::any& value)
		{
			return value.has_value() && value.type() == typeid(HWND) ? std::any_cast<HWND>(value) : nullptr;
		}

		::platform::cursor to_cursor(const CursorShape c)
		{
			switch (c)
			{
			case CursorShape::zoom: return ::platform::cursor::zoom;
			case CursorShape::sizeAll: return ::platform::cursor::size_all;
			case CursorShape::sizeHorizontal: return ::platform::cursor::left_right;
			default: return ::platform::cursor::normal;
			}
		}

		KeyCode key_code(const char32_t key)
		{
			if (key == ::platform::keys::LEFT) return KeyCode::left;
			if (key == ::platform::keys::RIGHT) return KeyCode::right;
			if (key == ::platform::keys::UP) return KeyCode::up;
			if (key == ::platform::keys::DOWN) return KeyCode::down;
			if (key == ::platform::keys::HOME) return KeyCode::home;
			if (key == ::platform::keys::END) return KeyCode::end;
			if (key == ::platform::keys::PRIOR) return KeyCode::pageUp;
			if (key == ::platform::keys::NEXT) return KeyCode::pageDown;
			if (key == ::platform::keys::SPACE) return KeyCode::space;
			if (key == ::platform::keys::DEL) return KeyCode::deleteKey;
			if (key == ::platform::keys::F2) return KeyCode::f2;
			if (key == ::platform::keys::F7) return KeyCode::f7;
			if (key == ::platform::keys::RETURN) return KeyCode::enter;
			if (key == ::platform::keys::ESCAPE) return KeyCode::escape;
			if (key == ::platform::keys::DIVIDE) return KeyCode::divide;
			if (key == ::platform::keys::MULTIPLY) return KeyCode::multiply;
			if (key == ::platform::keys::ADD) return KeyCode::add;
			if (key == ::platform::keys::SUBTRACT) return KeyCode::subtract;
			return KeyCode::unknown;
		}

		// The longest prefix that fits, never ending between the halves of a surrogate pair.
		std::wstring fitting_prefix(const std::wstring_view text, const int available,
		                            const std::function<int(std::wstring_view)>& measure)
		{
			size_t low = 0, high = text.size();
			while (low < high)
			{
				const size_t middle = low + (high - low + 1) / 2;
				if (measure(text.substr(0, middle)) <= available) low = middle;
				else high = middle - 1;
			}
			if (low > 0 && low < text.size() && text[low - 1] >= 0xd800 && text[low - 1] <= 0xdbff &&
			    text[low] >= 0xdc00 && text[low] <= 0xdfff)
				--low;
			return std::wstring(text.substr(0, low));
		}

		class LibdesktopWindowFrame;
		class LibdesktopFrameHost;

		class LibdesktopDrawContext final : public DrawContext
		{
		public:
			explicit LibdesktopDrawContext(render::draw_context& draw) : draw_(draw) {}

			app::recti clip_rect() const override { return app_rect(draw_.clip_bounds()); }

			void fill(const app::recti r, const color c) override { draw_.draw_rect(core_rect(r), core_color(c)); }

			void blend_fill(const app::recti r, const color c) override
			{
				draw_.draw_rect(core_rect(r), core_color(c));
			}

			void text(std::wstring_view, app::recti, color, std::uint32_t, const FontPtr&) override;

			app::sizei measure_text(std::wstring_view value, std::uint32_t, const FontPtr& font) override
			{
				if (value.empty()) return {};
				const auto face = font && *font ? static_cast<render::font_face>(font->native_handle())
				                                : render::font_face::dialog;
				return app_size(draw_.measure_text(::core::utf16_to_utf8(value), face, render::text_style::single_line,
				                                   (std::numeric_limits<int>::max)()));
			}

			void image(std::span<const std::uint32_t> pixels, app::sizei source, app::recti destination, bool) override
			{
				if (source.is_empty() || destination.is_empty() ||
				    pixels.size() < static_cast<size_t>(source.width) * source.height)
					return;
				auto surface = std::make_shared<core::surface>();
				surface->alloc(core_size(source), core::texture_format::ARGB);
				for (int y = 0; y < source.height; ++y)
					std::memcpy(surface->pixels_line(y), pixels.data() + static_cast<size_t>(y) * source.width,
					            static_cast<size_t>(source.width) * sizeof(std::uint32_t));
				render::draw_surface(draw_, surface, core_rect(destination), 1.0f);
			}

			void focus(const app::recti r) override { outline(r, {0, 0, 0}, 1); }

			void outline(const app::recti r, const color c, const int width) override
			{
				if (r.is_empty()) return;
				const auto clr = core_color(c);
				draw_.draw_line({static_cast<double>(r.x), static_cast<double>(r.y)},
				                {static_cast<double>(r.right()), static_cast<double>(r.y)}, clr, width);
				draw_.draw_line({static_cast<double>(r.right()), static_cast<double>(r.y)},
				                {static_cast<double>(r.right()), static_cast<double>(r.bottom())}, clr, width);
				draw_.draw_line({static_cast<double>(r.right()), static_cast<double>(r.bottom())},
				                {static_cast<double>(r.x), static_cast<double>(r.bottom())}, clr, width);
				draw_.draw_line({static_cast<double>(r.x), static_cast<double>(r.bottom())},
				                {static_cast<double>(r.x), static_cast<double>(r.y)}, clr, width);
			}

			void clip(const app::recti r) override { draw_.clip_bounds(core_rect(r)); }

			void reset_clip() override { draw_.restore_clip(); }

		private:
			render::draw_context& draw_;
		};

		class LibdesktopMeasureContext final : public MeasureContext
		{
		public:
			explicit LibdesktopMeasureContext(render::measure_context& measure) : measure_(measure) {}

			app::sizei measure_text(std::wstring_view value, std::uint32_t, const FontPtr& font) const override
			{
				if (value.empty()) return {};
				const auto face = font && *font ? static_cast<render::font_face>(font->native_handle())
				                                : render::font_face::dialog;
				return app_size(measure_.measure_text(::core::utf16_to_utf8(value), face,
				                                      render::text_style::single_line,
				                                      (std::numeric_limits<int>::max)()));
			}

		private:
			render::measure_context& measure_;
		};

		class LibdesktopWindowFrame final :
			public WindowFrame,
			public std::enable_shared_from_this<LibdesktopWindowFrame>
		{
		public:
			LibdesktopWindowFrame(::ui::frame_ptr native, std::shared_ptr<LibdesktopFrameHost> host) :
				native_(std::move(native)), host_(std::move(host))
			{
			}

			void attach_owner();
			void set_menu_bar(std::vector<::ui::command_ptr> menus);

			bool created_ok() const { return created_ok_; }

			::ui::frame_ptr ui_frame() const { return native_; }

			const std::shared_ptr<LibdesktopFrameHost>& host() const { return host_; }

			NativeHandle native_handle() const override
			{
				return native_ ? reinterpret_cast<NativeHandle>(hwnd_from_any(native_->handle())) : 0;
			}

			void set_reactor(FrameReactorPtr reactor) override;
			WindowFramePtr create_child(FrameReactorPtr reactor, const WindowOptions& options) override;

			app::recti client_rect() const override
			{
				RECT r{};
				const auto hwnd = reinterpret_cast<HWND>(native_handle());
				return hwnd && GetClientRect(hwnd, &r) ? app::recti{r.left, r.top, r.right - r.left, r.bottom - r.top}
				                                       : app::recti{};
			}

			void move(app::recti r) override
			{
				pendingBounds_ = r;
				if (native_) native_->window_bounds(core_rect(r), pendingVisible_);
			}

			void show(bool v) override
			{
				pendingVisible_ = v;
				if (native_) native_->show(v);
			}

			void invalidate() override
			{
				if (native_) native_->invalidate();
			}

			void invalidate(app::recti r) override
			{
				if (native_) native_->invalidate(core_rect(r));
			}

			void set_focus() override
			{
				if (native_) native_->focus();
			}

			bool has_focus() const override { return native_ && native_->has_focus(); }

			void set_capture() override
			{
				if (const auto h = reinterpret_cast<HWND>(native_handle())) SetCapture(h);
			}

			void release_capture() override
			{
				if (GetCapture() == reinterpret_cast<HWND>(native_handle())) ReleaseCapture();
			}

			void track_mouse_leave() override
			{
				if (const auto h = reinterpret_cast<HWND>(native_handle()))
				{
					TRACKMOUSEEVENT e{sizeof(e), TME_LEAVE, h, 0};
					TrackMouseEvent(&e);
				}
			}

			void configure_gestures(bool, bool) override
			{
				// libdesktop routes native gestures to every frame host; no per-window registration is needed.
			}

			void start_timer(unsigned int) override {}

			void stop_timer() override {}

			void accept_file_drops(bool value = true) override { acceptsDrops_ = value; }

			void set_cursor(CursorShape c) override
			{
				if (native_) native_->set_cursor(to_cursor(c));
			}

			app::pointi screen_to_client(app::pointi p) const override
			{
				POINT q{p.x, p.y};
				const auto h = reinterpret_cast<HWND>(native_handle());
				return h && ScreenToClient(h, &q) ? app::pointi{q.x, q.y} : p;
			}

			app::pointi client_to_screen(app::pointi p) const override
			{
				POINT q{p.x, p.y};
				const auto h = reinterpret_cast<HWND>(native_handle());
				return h && ClientToScreen(h, &q) ? app::pointi{q.x, q.y} : p;
			}

			void set_title(std::wstring_view t) override
			{
				pendingTitle_ = t;
				if (native_) native_->window_text(::core::utf16_to_utf8(t));
			}

			void set_fullscreen(bool value) override
			{
				if (native_) native_->full_screen(value);
			}

			void set_maximized(bool value) override
			{
				pendingMaximized_ = value;
				if (native_)
					native_->sys_command(value ? ::platform::sys_command_type::MAXIMIZE
					                           : ::platform::sys_command_type::RESTORE);
			}

			WindowPlacement placement() const override
			{
				WINDOWPLACEMENT p{sizeof(p)};
				const auto h = reinterpret_cast<HWND>(native_handle());
				if (h && GetWindowPlacement(h, &p))
				{
					const auto& r = p.rcNormalPosition;
					return {{r.left, r.top, r.right - r.left, r.bottom - r.top}, p.showCmd == SW_MAXIMIZE};
				}
				return {pendingBounds_, pendingMaximized_};
			}

			void set_accelerators(std::span<const CommandAccelerator>) override {}

			void close() override
			{
				if (native_) native_->close();
			}

			unsigned int dpi() const override
			{
				return native_ ? static_cast<unsigned int>(native_->scale_factor() * 96.0 + 0.5) : 96;
			}

		private:
			::ui::frame_ptr native_;
			std::shared_ptr<LibdesktopFrameHost> host_;
			FrameReactorPtr reactor_;
			app::recti pendingBounds_{};
			std::wstring pendingTitle_;
			bool pendingVisible_{true};
			bool pendingMaximized_{};
			bool acceptsDrops_{};
			bool created_{};
			bool created_ok_{true};
		};

		class LibdesktopFrameHost final :
			public ::ui::frame_host,
			public std::enable_shared_from_this<LibdesktopFrameHost>
		{
		public:
			// An element drawn over the reactor's painting, laid out at the bounds its owner gave it.
			struct Hosted
			{
				::ui::element_ptr element;
				core::recti bounds;
				bool placed{};
			};

			explicit LibdesktopFrameHost(FrameReactorPtr reactor) : reactor_(std::move(reactor)) {}

			void set_owner(const std::shared_ptr<LibdesktopWindowFrame>& owner) { owner_ = owner; }

			std::shared_ptr<LibdesktopWindowFrame> owner() const { return owner_.lock(); }

			void add_child(const std::shared_ptr<LibdesktopFrameHost>& child)
			{
				if (child) children_.push_back(child);
			}

			void set_reactor(FrameReactorPtr reactor) { reactor_ = std::move(reactor); }

			std::shared_ptr<Hosted> add_element(::ui::element_ptr element)
			{
				auto entry = std::make_shared<Hosted>(Hosted{std::move(element)});
				hosted_.push_back(entry);
				return entry;
			}

			void remove_element(const Hosted& entry)
			{
				const auto bounds = entry.bounds;
				const bool visible = entry.element && entry.element->is_visible();
				std::erase_if(hosted_, [&entry](const auto& item) { return item.get() == &entry; });
				if (visible) invalidate(bounds);
			}

			void place_element(Hosted& entry, const core::recti bounds)
			{
				if (entry.bounds == bounds) return;
				const auto previous = entry.bounds;
				entry.bounds = bounds;
				entry.placed = false;
				// A paint lays an element out before drawing it, so only a move between paints asks for one.
				if (!painting_ && entry.element->is_visible()) invalidate(previous.make_union(bounds));
			}

			void show_element(const Hosted& entry, const bool visible)
			{
				if (entry.element->is_visible() == visible) return;
				entry.element->is_visible(visible);
				if (!painting_) invalidate(entry.bounds);
			}

			void release_graphics()
			{
				const ::ui::element_event event{::ui::element_event_type::free_graphics_resources, nullptr};
				for (const auto& entry : std::vector(hosted_)) release_graphics(entry->element, event);
				if (const auto owner = owner_.lock(); owner && !hosted_.empty()) owner->invalidate();
			}

			void set_menu_bar(std::vector<::ui::command_ptr> menus)
			{
				if (menus.empty())
				{
					if (menuBar_) menuBar_->is_visible(false);
				}
				else if (!menuBar_)
				{
					menuBar_ = ::ui::make_menu_bar(std::move(menus));
					menuBar_->is_visible(true);
				}
				else menuBar_->is_visible(true);
				if (const auto owner = owner_.lock()) owner->invalidate();
			}

			void on_window_layout(render::measure_context& mc, core::sizei extent, bool minimized) override
			{
				extent_ = extent;
				if (menuBar_ && !minimized)
				{
					menuHeight_ = (std::max)(0, menuBar_->measure(mc, extent.cx).cy);
					menuBar_->layout(mc, {0, 0, extent.cx, menuHeight_});
				}
				else menuHeight_ = 0;
				if (minimized) return;
				if (const auto owner = owner_.lock(); owner && reactor_)
				{
					LibdesktopMeasureContext measure(mc);
					reactor_->size(owner, app_size(extent), measure);
				}
				for (const auto& entry : std::vector(hosted_)) place(mc, *entry);
			}

			void on_window_paint(render::draw_context& dc) override
			{
				dc.colors = frame_colors();
				dc.draw_rect(core::recti(extent_), core::color(dc.colors.background, 1.0f));
				if (menuBar_ && menuBar_->is_visible()) menuBar_->render(dc, {});
				painting_ = true;
				const core::scope_exit painted([this] { painting_ = false; });
				if (const auto owner = owner_.lock(); owner && reactor_)
				{
					LibdesktopDrawContext draw(dc);
					reactor_->paint(owner, draw);
				}
				// The reactor places its elements as it lays its own drawing out, so they follow it.
				for (const auto& entry : std::vector(hosted_))
				{
					if (!entry->placed) place(dc, *entry);
					if (entry->element->is_visible() && !entry->bounds.is_empty()) entry->element->render(dc, {});
				}
			}

			void on_window_destroy() override
			{
				if (const auto owner = owner_.lock(); owner && reactor_)
					reactor_->message(owner, WindowMessage::destroy);
			}

			void focus_changed(bool has_focus, const ::ui::control_ptr&) override
			{
				if (const auto owner = owner_.lock(); owner && reactor_)
					reactor_->message(owner, has_focus ? WindowMessage::focusGained : WindowMessage::focusLost);
			}

			void dpi_changed() override
			{
				if (const auto owner = owner_.lock(); owner && reactor_)
					reactor_->message(owner, WindowMessage::dpiChanged);
			}

			void on_mouse_move(core::pointi p, bool) override { call_mouse(MouseMessage::move, p); }

			void on_mouse_leave(core::pointi) override { call_mouse(MouseMessage::leave, {}); }

			void on_mouse_left_button_down(core::pointi p, ::platform::key_state k) override
			{
				call_mouse(MouseMessage::leftButtonDown, p, k);
			}

			void on_mouse_left_button_up(core::pointi p, ::platform::key_state k) override
			{
				call_mouse(MouseMessage::leftButtonUp, p, k);
			}

			void on_mouse_right_button_down(core::pointi p, ::platform::key_state k) override
			{
				call_mouse(MouseMessage::rightButtonDown, p, k);
			}

			void on_mouse_right_button_up(core::pointi p, ::platform::key_state k) override
			{
				call_mouse(MouseMessage::rightButtonUp, p, k);
			}

			void on_mouse_left_button_double_click(core::pointi p, ::platform::key_state k) override
			{
				call_mouse(MouseMessage::leftButtonDoubleClick, p, k);
			}

			void on_mouse_wheel(core::pointi p, int delta, ::platform::key_state k, bool& handled) override
			{
				handled = call_mouse(MouseMessage::wheel, p, k, delta) != 0;
			}

			void on_pinch(core::pointi p, int delta, bool begins, bool& handled) override
			{
				handled = false;
				if (begins) pinchDistance_ = 480.0;
				else if (pinchDistance_ > 0.0) pinchDistance_ *= std::exp(static_cast<double>(delta) / 480.0);
				if (const auto owner = owner_.lock(); owner && reactor_ && pinchDistance_ > 0.0)
				{
					GestureInput i;
					i.kind = GestureKind::zoom;
					i.point = {p.x, p.y};
					i.argument = static_cast<std::uint64_t>((std::max)(1.0, std::round(pinchDistance_)));
					i.begin = begins;
					handled = reactor_->gesture(owner, i) != 0;
				}
			}

			void pan_start(core::pointi p) override { call_pan(p, true, false); }

			void pan(core::pointi, core::pointi current) override { call_pan(current, false, false); }

			void pan_end(core::pointi, core::pointi final) override { call_pan(final, false, true); }

			bool key_down(char32_t c, ::platform::key_state k) override
			{
				if (const auto owner = owner_.lock(); owner && reactor_)
				{
					KeyInput i;
					i.key = key_code(c);
					i.character = c <= 0xffff ? static_cast<wchar_t>(c) : L'\0';
					i.control = k.control;
					i.shift = k.shift;
					i.alt = k.alt;
					return reactor_->key(owner, KeyMessage::down, i) != 0;
				}
				return false;
			}

			::platform::drop_effect drag_over(const ::platform::clipboard_data& d, ::platform::key_state,
			                                  core::pointi) override
			{
				return d.has_drop_files() ? ::platform::drop_effect::copy : ::platform::drop_effect::none;
			}

			::platform::drop_effect drag_drop(::platform::clipboard_data& d, ::platform::key_state,
			                                  core::pointi p) override
			{
				if (!d.has_drop_files()) return ::platform::drop_effect::none;
				if (const auto owner = owner_.lock(); owner && reactor_)
				{
					FileDrop f;
					f.point = {p.x, p.y};
					for (const auto& path : d.drop_paths()) f.files.push_back(::platform::to_stream_path(path));
					reactor_->files_dropped(owner, f);
				}
				return ::platform::drop_effect::copy;
			}

			void visit_elements(const std::function<void(const ::ui::element_ptr&, core::pointi)>& visit) override
			{
				if (menuBar_ && menuBar_->is_visible()) visit(menuBar_, {});
				for (const auto& entry : std::vector(hosted_)) visit(entry->element, {});
			}

			std::vector<::ui::command_ptr> menu(core::pointi screen) override
			{
				for (auto it = children_.rbegin(); it != children_.rend(); ++it)
				{
					if (const auto child = it->lock(); child && child->contains_screen(screen))
						return child->menu(screen);
				}
				if (const auto owner = owner_.lock(); owner && reactor_)
				{
					MouseInput i;
					i.screenPoint = {screen.x, screen.y};
					i.point = owner->screen_to_client(i.screenPoint);
					const auto k = ::platform::current_key_state();
					i.control = k.control;
					i.shift = k.shift;
					reactor_->mouse(owner, MouseMessage::contextMenu, i);
				}
				return {};
			}

			void tick() override
			{
				if (const auto owner = owner_.lock(); owner && reactor_) reactor_->message(owner, WindowMessage::timer);
			}

		private:
			static void place(render::measure_context& mc, Hosted& entry)
			{
				entry.element->layout(mc, entry.bounds);
				entry.placed = true;
			}

			static void release_graphics(const ::ui::element_ptr& element, const ::ui::element_event& event)
			{
				if (!element) return;
				element->dispatch_event(event);
				element->visit_children([&event](const ::ui::element_ptr& child) { release_graphics(child, event); });
			}

			void invalidate(const core::recti bounds) const
			{
				if (bounds.is_empty()) return;
				if (const auto owner = owner_.lock()) owner->invalidate(app_rect(bounds));
			}

			bool contains_screen(const core::pointi screen) const
			{
				const auto owner = owner_.lock();
				if (!owner) return false;
				const auto handle = reinterpret_cast<HWND>(owner->native_handle());
				if (handle && !IsWindowVisible(handle)) return false;
				return owner->client_rect().contains(owner->screen_to_client({screen.x, screen.y}));
			}

			MessageResult call_mouse(MouseMessage m, core::pointi p, ::platform::key_state k = {}, int wheel = 0)
			{
				if (const auto owner = owner_.lock(); owner && reactor_)
				{
					MouseInput i;
					i.point = {p.x, p.y};
					i.screenPoint = owner->client_to_screen(i.point);
					i.leftButton = GetKeyState(VK_LBUTTON) < 0;
					i.control = k.control;
					i.shift = k.shift;
					i.wheelDelta = wheel;
					return reactor_->mouse(owner, m, i);
				}
				return 0;
			}

			void call_pan(core::pointi p, const bool begin, const bool end)
			{
				if (const auto owner = owner_.lock(); owner && reactor_)
				{
					GestureInput i;
					i.kind = GestureKind::pan;
					i.point = {p.x, p.y};
					i.begin = begin;
					i.end = end;
					reactor_->gesture(owner, i);
				}
			}

			std::weak_ptr<LibdesktopWindowFrame> owner_;
			FrameReactorPtr reactor_;
			::ui::control_ptr menuBar_;
			std::vector<std::shared_ptr<Hosted>> hosted_;
			core::sizei extent_{};
			int menuHeight_{};
			double pinchDistance_{};
			bool painting_{};
			std::vector<std::weak_ptr<LibdesktopFrameHost>> children_;
		};

		void LibdesktopWindowFrame::attach_owner()
		{
			if (host_) host_->set_owner(shared_from_this());
		}

		void LibdesktopWindowFrame::set_reactor(FrameReactorPtr reactor)
		{
			if (!reactor && reactor_ && created_) reactor_->message(shared_from_this(), WindowMessage::destroy);
			reactor_ = std::move(reactor);
			if (host_) host_->set_reactor(reactor_);
			if (reactor_ && !created_)
			{
				created_ = true;
				created_ok_ = reactor_->message(shared_from_this(), WindowMessage::create) >= 0;
				if (created_ok_ && native_) native_->layout();
			}
		}

		WindowFramePtr LibdesktopWindowFrame::create_child(FrameReactorPtr reactor, const WindowOptions& options)
		{
			if (!native_) return {};
			auto host = std::make_shared<LibdesktopFrameHost>(reactor);
			::ui::frame_style style;
			style.child = true;
			style.container = !reactor;
			style.can_drop = true;
			style.hardware_accelerated = true;
			style.timer_milliseconds = 50;
			style.colors = frame_colors();
			auto child = native_->create_frame(host, style);
			if (!child) return {};
			if (host_) host_->add_child(host);
			auto result = std::make_shared<LibdesktopWindowFrame>(std::move(child), std::move(host));
			result->attach_owner();
			result->set_reactor(std::move(reactor));
			if (!result->created_ok()) return {};
			if (!options.title.empty()) result->set_title(options.title);
			if (!options.bounds.is_empty()) result->move(options.bounds);
			result->show(options.visible);
			return result;
		}

		void LibdesktopWindowFrame::set_menu_bar(std::vector<::ui::command_ptr> menus)
		{
			if (host_) host_->set_menu_bar(std::move(menus));
		}

		// The hosts that draw elements, which give up their textures when the graphics device is lost.
		std::vector<std::weak_ptr<LibdesktopFrameHost>>& element_hosts()
		{
			static std::vector<std::weak_ptr<LibdesktopFrameHost>> hosts;
			return hosts;
		}

		class HostedFrameElement final : public FrameElement
		{
		public:
			HostedFrameElement(const std::shared_ptr<LibdesktopFrameHost>& host,
			                   std::shared_ptr<LibdesktopFrameHost::Hosted> entry) :
				host_(host), entry_(std::move(entry))
			{
			}

			~HostedFrameElement() override
			{
				if (const auto host = host_.lock()) host->remove_element(*entry_);
			}

			HostedFrameElement(const HostedFrameElement&) = delete;
			HostedFrameElement& operator=(const HostedFrameElement&) = delete;

			void set_bounds(const app::recti bounds) override
			{
				if (const auto host = host_.lock()) host->place_element(*entry_, core_rect(bounds));
			}

			void show(const bool visible) override
			{
				if (const auto host = host_.lock()) host->show_element(*entry_, visible);
				else entry_->element->is_visible(visible);
			}

			::ui::frame_ptr frame() const override
			{
				const auto host = host_.lock();
				const auto owner = host ? host->owner() : nullptr;
				return owner ? owner->ui_frame() : nullptr;
			}

		private:
			std::weak_ptr<LibdesktopFrameHost> host_;
			std::shared_ptr<LibdesktopFrameHost::Hosted> entry_;
		};

		class NullWindowEvents final : public ::platform::window_events
		{
		};

		class NativeTestWindowFrame final :
			public WindowFrame,
			public std::enable_shared_from_this<NativeTestWindowFrame>
		{
		public:
			NativeTestWindowFrame(::platform::window_ptr native, std::shared_ptr<NullWindowEvents> events) :
				native_(std::move(native)), events_(std::move(events))
			{
			}

			NativeHandle native_handle() const override
			{
				return native_ ? reinterpret_cast<NativeHandle>(hwnd_from_any(native_->native_handle())) : 0;
			}

			void set_reactor(FrameReactorPtr) override {}

			WindowFramePtr create_child(FrameReactorPtr, const WindowOptions& o) override
			{
				auto e = std::make_shared<NullWindowEvents>();
				if (!native_) return std::make_shared<NativeTestWindowFrame>(::platform::window_ptr{}, std::move(e));
				::platform::window_options n;
				n.kind = ::platform::window_kind::child;
				n.parent = native_.get();
				n.visible = o.visible;
				n.bounds = core_rect(o.bounds);
				n.client_size = core_size(o.bounds.extent());
				auto c = ::platform::create_window(n, e);
				return c ? std::make_shared<NativeTestWindowFrame>(std::move(c), std::move(e)) : WindowFramePtr{};
			}

			app::recti client_rect() const override
			{
				return native_ ? app_size(native_->client_extent())
				               : app::recti{0, 0, pending_.width > 0 ? pending_.width : 1000,
				                            pending_.height > 0 ? pending_.height : 700};
			}

			void move(app::recti r) override
			{
				pending_ = r;
				if (native_) native_->set_bounds(core_rect(r), visible_);
			}

			void show(bool v) override
			{
				visible_ = v;
				if (native_) native_->show(v);
			}

			void invalidate() override {}

			void invalidate(app::recti) override {}

			void set_focus() override
			{
				if (native_) native_->focus();
				else focused_ = true;
			}

			bool has_focus() const override { return native_ ? native_->has_focus() : focused_; }

			void set_capture() override
			{
				if (native_) native_->capture(true);
			}

			void release_capture() override
			{
				if (native_) native_->capture(false);
			}

			void track_mouse_leave() override {}

			void configure_gestures(bool, bool) override {}

			void start_timer(unsigned int) override {}

			void stop_timer() override {}

			void accept_file_drops(bool) override {}

			void set_cursor(CursorShape c) override
			{
				if (native_) native_->set_cursor(to_cursor(c));
			}

			app::pointi screen_to_client(app::pointi p) const override
			{
				return native_ ? app::pointi(native_->screen_to_client(static_cast<core::pointi>(p))) : p;
			}

			app::pointi client_to_screen(app::pointi p) const override
			{
				return native_ ? app::pointi(native_->client_to_screen(static_cast<core::pointi>(p))) : p;
			}

			void set_title(std::wstring_view t) override
			{
				if (native_) native_->set_title(::core::utf16_to_utf8(t));
			}

			void set_fullscreen(bool) override {}

			void set_maximized(bool v) override
			{
				maximized_ = v;
				if (native_)
					native_->sys_command(v ? ::platform::sys_command_type::MAXIMIZE
					                       : ::platform::sys_command_type::RESTORE);
			}

			WindowPlacement placement() const override
			{
				return {native_ ? app_rect(native_->bounds()) : pending_, maximized_};
			}

			void set_accelerators(std::span<const CommandAccelerator>) override {}

			void close() override
			{
				if (native_) native_->destroy();
			}

			unsigned int dpi() const override
			{
				return native_ ? static_cast<unsigned int>(native_->scale() * 96.0 + 0.5) : 96;
			}

		private:
			::platform::window_ptr native_;
			std::shared_ptr<NullWindowEvents> events_;
			app::recti pending_{};
			bool visible_{true};
			bool maximized_{};
			bool focused_{};
		};

		::ui::frame_ptr pendingMainFrame;
		std::shared_ptr<LibdesktopFrameHost> pendingMainHost;
		std::vector<::ui::command_ptr> activeMenuCommands;

		::platform::setting_file_ptr& settings_store()
		{
			static auto store = []
			{
				auto folder = ::platform::running_app_path().folder();
				if (folder.is_empty()) folder = ::platform::current_directory();
				return ::platform::create_ini_file_settings(folder);
			}();
			return store;
		}

		::platform::keyboard_accelerator_t to_keyboard_accelerator(const Shortcut s)
		{
			::platform::keyboard_accelerator_t r;
			switch (s.key)
			{
			case ShortcutKey::a: r.key = 'A'; break;
			case ShortcutKey::b: r.key = 'B'; break;
			case ShortcutKey::c: r.key = 'C'; break;
			case ShortcutKey::e: r.key = 'E'; break;
			case ShortcutKey::n: r.key = 'N'; break;
			case ShortcutKey::o: r.key = 'O'; break;
			case ShortcutKey::r: r.key = 'R'; break;
			case ShortcutKey::v: r.key = 'V'; break;
			case ShortcutKey::x: r.key = 'X'; break;
			case ShortcutKey::z: r.key = 'Z'; break;
			case ShortcutKey::left: r.key = ::platform::keys::LEFT; break;
			case ShortcutKey::right: r.key = ::platform::keys::RIGHT; break;
			case ShortcutKey::up: r.key = ::platform::keys::UP; break;
			case ShortcutKey::deleteKey: r.key = ::platform::keys::DEL; break;
			case ShortcutKey::enterKey: r.key = ::platform::keys::RETURN; break;
			case ShortcutKey::f1: r.key = ::platform::keys::F1; break;
			case ShortcutKey::f2: r.key = ::platform::keys::F2; break;
			case ShortcutKey::f3: r.key = ::platform::keys::F3; break;
			case ShortcutKey::f4: r.key = ::platform::keys::F4; break;
			case ShortcutKey::f5: r.key = ::platform::keys::F5; break;
			case ShortcutKey::f6: r.key = ::platform::keys::F6; break;
			case ShortcutKey::f7: r.key = ::platform::keys::F7; break;
			case ShortcutKey::f8: r.key = ::platform::keys::F8; break;
			case ShortcutKey::f9: r.key = ::platform::keys::F9; break;
			case ShortcutKey::f11: r.key = ::platform::keys::F11; break;
			case ShortcutKey::f12: r.key = ::platform::keys::F12; break;
			default: break;
			}
			if (s.control) r.key_state |= ::platform::keyboard_accelerator_t::control;
			if (s.shift) r.key_state |= ::platform::keyboard_accelerator_t::shift;
			if (s.alt) r.key_state |= ::platform::keyboard_accelerator_t::alt;
			return r;
		}

		::ui::command_ptr ui_command(const CommandPtr& source, const std::wstring& label = {})
		{
			if (!source) return {};
			auto r = std::make_shared<::ui::command>();
			r->text = ::core::utf16_to_utf8(label.empty() ? source->name : label);
			r->toolbar_text = source->toolbarText ? ::core::utf16_to_utf8(source->toolbarText()) : r->text;
			r->tooltip_text = ::core::utf16_to_utf8(source->tooltip);
			const auto accelerator = to_keyboard_accelerator(source->shortcut);
			if (accelerator.key)
			{
				r->kba.push_back(accelerator);
				r->keyboard_accelerator_text = ::platform::format_keyboard_accelerator(r->kba);
			}
			r->enable = !source->enabled || source->enabled();
			r->checked = source->checked && source->checked();
			r->invoke = source->invoke;
			return r;
		}

		std::vector<::ui::command_ptr> ui_menu_items(const std::vector<MenuItem>& items)
		{
			std::vector<::ui::command_ptr> result;
			result.reserve(items.size());
			for (const auto& item : items)
			{
				if (item.kind == MenuItemKind::separator)
				{
					result.push_back(nullptr);
					continue;
				}
				if (item.kind == MenuItemKind::submenu)
				{
					auto r = std::make_shared<::ui::command>();
					r->text = ::core::utf16_to_utf8(item.name);
					const auto children = item.children;
					r->menu = [children]
					{
						return ui_menu_items(children);
					};
					result.push_back(std::move(r));
					continue;
				}
				result.push_back(ui_command(item.command));
			}
			return result;
		}

		std::vector<::ui::command_ptr> ui_menus(const std::vector<Menu>& menus)
		{
			std::vector<::ui::command_ptr> result;
			result.reserve(menus.size());
			for (const auto& menu : menus)
			{
				auto r = std::make_shared<::ui::command>();
				r->text = ::core::utf16_to_utf8(menu.name);
				const auto children = menu.items;
				r->menu = [children]
				{
					return ui_menu_items(children);
				};
				result.push_back(std::move(r));
			}
			return result;
		}

		// One background thread draining one FIFO. Work is abandoned, not finished, on stop.
		class SerialQueue
		{
		public:
			~SerialQueue() { stop(); }

			bool push(std::function<void()> work)
			{
				{
					const std::scoped_lock lock(mutex_);
					if (stopping_) return false;
					queue_.push_back(std::move(work));
					if (!thread_.joinable()) thread_ = std::thread(&SerialQueue::run, this);
				}
				ready_.notify_one();
				return true;
			}

			void stop()
			{
				{
					const std::scoped_lock lock(mutex_);
					stopping_ = true;
				}
				ready_.notify_all();
				if (thread_.joinable()) thread_.join();
				const std::scoped_lock lock(mutex_);
				queue_.clear();
			}

		private:
			void run()
			{
				const Runtime runtime(RuntimeMode::multithreaded);
				for (;;)
				{
					std::function<void()> work;
					{
						std::unique_lock lock(mutex_);
						ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
						if (stopping_) return;
						work = std::move(queue_.front());
						queue_.pop_front();
					}
					try
					{
						work();
					}
					catch (...)
					{
					}
				}
			}

			std::mutex mutex_;
			std::condition_variable ready_;
			std::deque<std::function<void()>> queue_;
			std::thread thread_;
			bool stopping_{};
		};

		SerialQueue& serial_queue(const WorkQueue q)
		{
			static std::array<SerialQueue, 6> queues;
			return queues[static_cast<size_t>(q)];
		}

		std::stop_source& exit_request()
		{
			static std::stop_source source;
			return source;
		}
	}

	struct Font::Impl
	{
		render::font_face face{render::font_face::dialog};
		bool valid{};
	};

	Font::Font() = default;

	Font::Font(std::unique_ptr<Impl> p) : impl_(std::move(p))
	{
	}

	Font::~Font() = default;
	Font::Font(Font&&) noexcept = default;
	Font& Font::operator=(Font&&) noexcept = default;

	Font::operator bool() const
	{
		return impl_ && impl_->valid;
	}

	NativeHandle Font::native_handle() const
	{
		return impl_ ? static_cast<NativeHandle>(static_cast<int>(impl_->face)) : 0;
	}

	FontPtr create_message_font(unsigned int)
	{
		// Its size and DPI come from the render context, just like libdesktop's controls.
		static const auto font = std::shared_ptr<Font>(
			new Font(std::make_unique<Font::Impl>(Font::Impl{render::font_face::dialog, true})));
		return font;
	}

	FontPtr command_icon_font(const unsigned int dpi)
	{
		return std::shared_ptr<Font>(new Font(std::make_unique<Font::Impl>(
			Font::Impl{render::make_font({"Segoe MDL2 Assets", MulDiv(18, dpi ? dpi : 96, 96)}), true})));
	}

	namespace win32
	{
		HWND owner_window(const WindowFramePtr& frame)
		{
			return frame ? reinterpret_cast<HWND>(frame->native_handle()) : nullptr;
		}

		void refresh_appearance(const HWND window)
		{
			HIGHCONTRASTW contrast{sizeof(contrast)};
			const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0) &&
			                          (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
			DWORD useLight = 1;
			DWORD size = sizeof(useLight);
			// Read the app preference, not the independent taskbar/SystemUsesLightTheme setting.
			const auto status = RegGetValueW(HKEY_CURRENT_USER,
			                                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			                                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &useLight, &size);
			currentAppearance = select_appearance(status != ERROR_SUCCESS || useLight != 0, highContrast);

			::ui::style::set_palette(appearance_palette(currentAppearance));
			if (window)
			{
				const BOOL darkCaption = currentAppearance == Appearance::dark;
				DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkCaption, sizeof(darkCaption));
				RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
			}
		}
	}

	color appearance_color(const Appearance appearance, const SystemColor v)
	{
		if (appearance == Appearance::highContrast)
		{
			const auto native = [](const int index)
			{
				return color::from_packed(GetSysColor(index));
			};
			switch (v)
			{
			case SystemColor::window: return native(COLOR_WINDOW);
			case SystemColor::windowText:
			case SystemColor::warningText: return native(COLOR_WINDOWTEXT);
			case SystemColor::face:
			case SystemColor::hover:
			case SystemColor::checked: return native(COLOR_BTNFACE);
			case SystemColor::shadow:
			case SystemColor::buttonText: return native(COLOR_BTNTEXT);
			case SystemColor::highlight: return native(COLOR_HIGHLIGHT);
			case SystemColor::highlightText: return native(COLOR_HIGHLIGHTTEXT);
			case SystemColor::grayText: return native(COLOR_GRAYTEXT);
			case SystemColor::scrollbar: return native(COLOR_SCROLLBAR);
			}
		}

		if (appearance == Appearance::dark)
		{
			switch (v)
			{
			case SystemColor::window: return {27, 30, 34};
			case SystemColor::windowText:
			case SystemColor::buttonText: return {236, 239, 244};
			case SystemColor::face: return {38, 42, 48};
			case SystemColor::shadow: return {116, 128, 145};
			case SystemColor::highlight: return {46, 83, 132};
			case SystemColor::highlightText: return {255, 255, 255};
			case SystemColor::grayText: return {178, 186, 197};
			case SystemColor::scrollbar: return {47, 53, 62};
			case SystemColor::hover: return {48, 64, 83};
			case SystemColor::checked: return {50, 70, 96};
			case SystemColor::warningText: return {255, 155, 134};
			}
		}

		switch (v)
		{
		case SystemColor::window: return app_color(::platform::system_color_value(::platform::system_color::window));
		case SystemColor::windowText:
			return app_color(::platform::system_color_value(::platform::system_color::window_text));
		case SystemColor::face: return {245, 245, 245};
		case SystemColor::shadow: return {128, 128, 128};
		case SystemColor::highlight: return {51, 103, 180};
		case SystemColor::highlightText: return {255, 255, 255};
		case SystemColor::grayText: return {101, 101, 101};
		case SystemColor::buttonText: return {32, 32, 32};
		case SystemColor::scrollbar: return {220, 220, 220};
		case SystemColor::hover: return {218, 232, 247};
		case SystemColor::checked: return {202, 220, 241};
		case SystemColor::warningText: return {176, 48, 32};
		}
		return colors::black;
	}

	color system_color(const SystemColor value)
	{
		return appearance_color(currentAppearance, value);
	}

	::ui::style::theme appearance_palette(const Appearance appearance)
	{
		const auto packed = [appearance](const SystemColor role)
		{
			return appearance_color(appearance, role).pack();
		};
		::ui::style::theme theme;
		theme.menu_background = packed(SystemColor::face);
		theme.menu_text = packed(SystemColor::buttonText);
		theme.menu_shortcut_text = packed(SystemColor::grayText);
		theme.toolbar_background = theme.menu_background;
		theme.view_background = packed(SystemColor::window);
		theme.view_text = packed(SystemColor::windowText);
		theme.view_selected_background = packed(SystemColor::highlight);
		theme.sidebar_background = theme.view_background;
		theme.bubble_background = theme.menu_background;
		theme.group_background = theme.menu_background;
		theme.dialog_background = theme.menu_background;
		theme.dialog_text = theme.menu_text;
		theme.dialog_selected_background = theme.view_selected_background;
		theme.dialog_selected_text = packed(SystemColor::highlightText);
		theme.button_background = packed(SystemColor::checked);
		theme.edit_background = theme.view_background;
		theme.edit_text = theme.view_text;
		theme.desktop_background = ::platform::system_color_value(::platform::system_color::desktop);
		theme.important_background = core::bgr(0xcc6611);
		theme.warning_background = core::bgr(0xaa2211);
		theme.success_background = core::bgr(0x2e8b33);
		theme.info_background = core::bgr(0x117799);
		return theme;
	}

	render::color_style frame_colors()
	{
		return {system_color(SystemColor::window).pack(), system_color(SystemColor::windowText).pack(),
		        system_color(SystemColor::highlight).pack()};
	}

	color calc_hande_color(bool hover, bool selected, bool checked)
	{
		if (selected) return system_color(SystemColor::highlight);
		if (hover) return system_color(SystemColor::hover);
		if (checked) return system_color(SystemColor::checked);
		return system_color(SystemColor::face);
	}

	app::sizei drag_threshold()
	{
		return {4, 4};
	}

	PopupItem PopupItem::action(CommandPtr command, std::wstring text)
	{
		return {std::move(command), std::move(text), false};
	}

	PopupItem PopupItem::separator_item()
	{
		PopupItem r;
		r.separator = true;
		return r;
	}

	void LibdesktopDrawContext::text(std::wstring_view value, app::recti bounds, color textColor, std::uint32_t format,
	                                 const FontPtr& font)
	{
		if (value.empty() || bounds.is_empty()) return;
		std::wstring shortened;
		std::wstring_view shown = value;
		const auto measure = [this, &font](std::wstring_view p)
		{
			return measure_text(p, TextFormat::singleLine, font).width;
		};
		auto extent = measure_text(shown, TextFormat::singleLine, font);
		if ((format & TextFormat::pathEllipsis) && extent.width > bounds.width)
		{
			shortened = path_ellipsis(value, bounds.width, measure);
			shown = shortened;
			extent = measure_text(shown, TextFormat::singleLine, font);
		}
		else if ((format & TextFormat::endEllipsis) && extent.width > bounds.width)
		{
			constexpr std::wstring_view dots = L"...";
			shortened = fitting_prefix(value, (std::max)(0, bounds.width - measure(dots)), measure);
			shortened += dots;
			shown = shortened;
			extent = measure_text(shown, TextFormat::singleLine, font);
		}
		int x = bounds.x;
		if (format & TextFormat::center) x += (bounds.width - extent.width) / 2;
		else if (format & TextFormat::right) x += bounds.width - extent.width;
		int y = bounds.y;
		if (format & TextFormat::verticalCenter) y += (bounds.height - extent.height) / 2;
		const auto face = font && *font ? static_cast<render::font_face>(font->native_handle())
		                                : render::font_face::dialog;
		draw_.draw_text(::core::utf16_to_utf8(shown), {x, y, x + extent.width, y + extent.height}, face,
		                render::text_style::single_line, core_color(textColor), core::color(0, 0, 0, 0));
	}

	std::wstring path_ellipsis(std::wstring_view path, int width, const std::function<int(std::wstring_view)>& measure)
	{
		if (!measure || measure(path) <= width) return std::wstring(path);
		constexpr std::wstring_view dots = L"...";
		std::wstring_view name = path;
		const auto end = path.find_last_not_of(L"\\/");
		const auto slash = end == std::wstring_view::npos ? end : path.find_last_of(L"\\/", end);
		if (slash != std::wstring_view::npos && slash > 0)
		{
			std::wstring kept(dots);
			kept.append(path.substr(slash));
			if (const int w = measure(kept); w <= width)
				return fitting_prefix(path.substr(0, slash), width - w, measure) + kept;
			name = path.substr(slash + 1);
			if (measure(name) <= width) return std::wstring(name);
		}
		return fitting_prefix(name, width - measure(dots), measure) + std::wstring(dots);
	}

	void show_popup_menu(const WindowFramePtr& owner, app::pointi screenPoint, std::span<const PopupItem> items)
	{
		const auto frame = std::dynamic_pointer_cast<LibdesktopWindowFrame>(owner);
		if (!frame) return;
		std::vector<::ui::command_ptr> commands;
		commands.reserve(items.size());
		for (const auto& item : items)
		{
			if (item.separator)
			{
				commands.push_back(nullptr);
				continue;
			}
			if (item.command) commands.push_back(ui_command(item.command, item.label));
		}
		if (const auto uiFrame = frame->ui_frame()) uiFrame->track_menu({screenPoint.x, screenPoint.y, 1, 1}, commands);
	}

	void set_frame_menu(const WindowFramePtr& owner, const std::vector<Menu>& menus)
	{
		const auto frame = std::dynamic_pointer_cast<LibdesktopWindowFrame>(owner);
		if (!frame) return;
		activeMenuCommands = ui_menus(menus);
		frame->set_menu_bar(activeMenuCommands);
	}

	bool dispatch_menu_accelerator(char32_t key, ::platform::key_state keys)
	{
		return ::ui::dispatch_accelerator(activeMenuCommands, key, keys);
	}

	::ui::frame_ptr native_ui_frame(const WindowFramePtr& frame)
	{
		if (const auto native = std::dynamic_pointer_cast<LibdesktopWindowFrame>(frame)) return native->ui_frame();
		return {};
	}

	FrameElementPtr add_frame_element(const WindowFramePtr& frame, ::ui::element_ptr element)
	{
		const auto native = std::dynamic_pointer_cast<LibdesktopWindowFrame>(frame);
		const auto host = native ? native->host() : nullptr;
		if (!host || !element) return {};
		auto& hosts = element_hosts();
		std::erase_if(hosts, [](const auto& weak) { return weak.expired(); });
		if (std::ranges::none_of(hosts, [&host](const auto& weak) { return weak.lock() == host; }))
			hosts.push_back(host);
		return std::make_unique<HostedFrameElement>(host, host->add_element(std::move(element)));
	}

	void release_graphics()
	{
		for (const auto& weak : std::vector(element_hosts()))
			if (const auto host = weak.lock()) host->release_graphics();
	}

	std::shared_ptr<::ui::frame_host> create_frame_host(FrameReactorPtr reactor)
	{
		return std::make_shared<LibdesktopFrameHost>(std::move(reactor));
	}

	void bind_main_frame(const ::ui::frame_ptr& frame, const std::shared_ptr<::ui::frame_host>& host)
	{
		pendingMainFrame = frame;
		pendingMainHost = std::dynamic_pointer_cast<LibdesktopFrameHost>(host);
	}

	WindowFramePtr create_top_level_frame(FrameReactorPtr reactor, const WindowOptions& options)
	{
		if (pendingMainFrame && pendingMainHost)
		{
			auto frame = std::make_shared<LibdesktopWindowFrame>(std::exchange(pendingMainFrame, nullptr),
			                                                     std::exchange(pendingMainHost, nullptr));
			frame->attach_owner();
			frame->set_reactor(std::move(reactor));
			if (!frame->created_ok()) return {};
			frame->set_title(options.title);
			if (!options.useDefaultPosition && !options.bounds.is_empty()) frame->move(options.bounds);
			frame->show(options.visible);
			frame->set_maximized(options.maximized);
			return frame;
		}
		auto events = std::make_shared<NullWindowEvents>();
		auto frame = std::make_shared<NativeTestWindowFrame>(::platform::window_ptr{}, std::move(events));
		frame->move(options.bounds);
		frame->show(options.visible);
		return frame;
	}

	BitmapResource load_bitmap_resource(const BitmapAsset asset)
	{
		BitmapResource result;
		const auto name = [asset]
		{
			switch (asset)
			{
			case BitmapAsset::fileFolder: return "folder.bmp";
			case BitmapAsset::filePhoto: return "photo.bmp";
			case BitmapAsset::fileDocument: return "document.bmp";
			case BitmapAsset::fileVideo: return "video.bmp";
			case BitmapAsset::fileAudio: return "audio.bmp";
			case BitmapAsset::fileOther: return "other.bmp";
			case BitmapAsset::toolbarSymbols: return "ToolbarSymbols.bmp";
			default: return "status-symbols.bmp";
			}
		}();
		const auto bytes = ::platform::embedded_resource_data(name);
		// BI_RGB leaves the high byte unused in ordinary BMPs. These embedded assets use it for straight alpha.
		const auto invalid = [name]
		{
			write_diagnostic(L"Invalid embedded bitmap: " + ::core::utf8_to_utf16(name) + L"\n");
			return BitmapResource{};
		};
		BITMAPFILEHEADER file{};
		BITMAPINFOHEADER info{};
		if (bytes.size() < sizeof(file) + sizeof(info)) return invalid();
		std::memcpy(&file, bytes.data(), sizeof(file));
		std::memcpy(&info, bytes.data() + sizeof(file), sizeof(info));
		if (file.bfType != 0x4d42 || info.biSize != sizeof(info) || info.biWidth <= 0 || info.biHeight == 0 ||
		    info.biHeight == (std::numeric_limits<LONG>::min)() || info.biPlanes != 1 || info.biBitCount != 32 ||
		    info.biCompression != BI_RGB || file.bfOffBits < sizeof(file) + sizeof(info) ||
		    file.bfOffBits > bytes.size())
			return invalid();
		const int height = std::abs(info.biHeight);
		const auto pixels = static_cast<std::uint64_t>(info.biWidth) * height;
		if (pixels > (bytes.size() - file.bfOffBits) / sizeof(std::uint32_t)) return invalid();
		result.size = {info.biWidth, height};
		result.pixels.resize(static_cast<size_t>(result.size.width) * result.size.height);
		const auto stride = static_cast<size_t>(info.biWidth) * sizeof(std::uint32_t);
		for (int y = 0; y < result.size.height; ++y)
		{
			const int row = info.biHeight < 0 ? y : height - y - 1;
			std::memcpy(result.pixels.data() + static_cast<size_t>(y) * result.size.width,
			            bytes.data() + file.bfOffBits + static_cast<size_t>(row) * stride, stride);
		}
		result.hasAlpha = std::ranges::any_of(result.pixels, [](std::uint32_t p) { return core::get_a(p) != 0xff; });
		for (auto& pixel : result.pixels)
			if (core::get_a(pixel) == 0) pixel = 0;
		return result;
	}

	bool queue_ui(std::function<void()> work)
	{
		if (!work) return false;
		::platform::run_ui(std::move(work));
		return true;
	}

	bool queue_work(WorkQueue q, std::function<void()> work)
	{
		return work && serial_queue(q).push(std::move(work));
	}

	std::stop_token exit_token()
	{
		return exit_request().get_token();
	}

	void shutdown_runtime()
	{
		exit_request().request_stop();
		for (const auto q : {WorkQueue::image, WorkQueue::thumbnail, WorkQueue::metadata, WorkQueue::folder,
		                     WorkQueue::shell, WorkQueue::task})
			serial_queue(q).stop();
	}

	int compare_ordinal_ignore_case(std::wstring_view l, std::wstring_view r)
	{
		const int v = CompareStringOrdinal(l.data(), static_cast<int>(l.size()), r.data(), static_cast<int>(r.size()),
		                                   TRUE);
		return v == 0 ? 0 : v - CSTR_EQUAL;
	}

	// StrCmpLogicalW needs null-terminated input; reuse per-thread buffers to keep sorting allocation free.
	int compare_file_names(std::wstring_view l, std::wstring_view r)
	{
		thread_local std::wstring a, b;
		a = l;
		b = r;
		return StrCmpLogicalW(a.c_str(), b.c_str());
	}

	std::optional<std::filesystem::path> choose_folder(const WindowFramePtr&, std::wstring_view title,
	                                                   const std::filesystem::path& initial)
	{
		auto selected = desktop::folder_path(initial);
		if (!::platform::browse_for_folder(nullptr, selected, ::core::utf16_to_utf8(title))) return {};
		return ::platform::to_stream_path(selected);
	}

	std::optional<std::filesystem::path> choose_open_file(const WindowFramePtr&, const OpenFileOptions& o)
	{
		std::vector<core::file_path> selected;
		const std::vector<::platform::file_dialog_filter> filters{{::core::utf16_to_utf8(o.filterName),
		                                                           ::core::utf16_to_utf8(o.filterPattern)},
		                                                          {"All files", "*.*"}};
		if (!::platform::prompt_for_open_paths(selected, filters, false) || selected.empty()) return {};
		return ::platform::to_stream_path(selected.front());
	}

	std::optional<std::filesystem::path> choose_save_file(const WindowFramePtr&, const SaveFileOptions& o)
	{
		core::file_path selected(desktop::utf8_path(o.initialFolder / o.initialName));
		const std::vector<::platform::file_dialog_filter> filters{
			{::core::utf16_to_utf8(o.filterName), ::core::utf16_to_utf8(o.filterPattern)}};
		if (!::platform::prompt_for_save_path(selected, filters)) return {};
		return ::platform::to_stream_path(selected);
	}

	int read_integer_setting(std::wstring_view section, std::wstring_view key, int fallback)
	{
		std::string text;
		if (!settings_store()->read(::core::utf16_to_utf8(section), ::core::utf16_to_utf8(key), text)) return fallback;
		int value = fallback;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? value : fallback;
	}

	void write_integer_settings(std::wstring_view section, std::span<const IntegerSetting> values)
	{
		for (const auto& value : values)
			settings_store()->write(::core::utf16_to_utf8(section), ::core::utf16_to_utf8(value.key),
			                        std::to_string(value.value));
	}

	std::wstring read_text_setting(std::wstring_view section, std::wstring_view key, std::wstring_view fallback)
	{
		std::string text;
		if (!settings_store()->read(::core::utf16_to_utf8(section), ::core::utf16_to_utf8(key), text))
			return std::wstring(fallback);
		return ::core::utf8_to_utf16(text);
	}

	void write_text_settings(std::wstring_view section, std::span<const TextSetting> values)
	{
		for (const auto& value : values)
			settings_store()->write(::core::utf16_to_utf8(section), ::core::utf16_to_utf8(value.key),
			                        ::core::utf16_to_utf8(value.value));
	}

	// The shared INI API intentionally writes through and does not expose a
	// destructive section delete.  Clear the bounded list keys used by 3.0;
	// empty values preserve the portable file and make stale rows invisible.
	void clear_settings_section(std::wstring_view section)
	{
		const auto s = ::core::utf16_to_utf8(section);
		for (int i = 0; i < 256; ++i)
		{
			settings_store()->write(s, "Path" + std::to_string(i), std::string_view{});
			settings_store()->write(s, "Collection" + std::to_string(i), std::string_view{});
		}
		for (const auto key : {"WholeCollection", "CollectionCount", "WindowLeft", "WindowTop", "WindowWidth",
		                       "WindowHeight", "Maximized", "ShowFolderTree", "FolderTreeWidth", "ThumbnailSize",
		                       "ViewMode", "FitImage", "PasteImageFormat"})
			settings_store()->write(s, key, std::string_view{});
	}

	std::filesystem::path module_folder()
	{
		const auto path = ::platform::running_app_path().folder();
		return path.is_empty() ? std::filesystem::current_path() : ::platform::to_stream_path(path);
	}

	std::filesystem::path known_folder(KnownFolder f)
	{
		::platform::known_folder id = ::platform::known_folder::pictures;
		switch (f)
		{
		case KnownFolder::documents: id = ::platform::known_folder::documents; break;
		case KnownFolder::desktop: id = ::platform::known_folder::desktop; break;
		case KnownFolder::downloads: id = ::platform::known_folder::downloads; break;
		case KnownFolder::music: id = ::platform::known_folder::music; break;
		case KnownFolder::videos: id = ::platform::known_folder::video; break;
		default: break;
		}
		const auto path = ::platform::known_path(id);
		if (path.is_empty()) return {};
		const auto result = ::platform::to_stream_path(path);
		std::error_code e;
		return std::filesystem::is_directory(result, e) && !e ? result : std::filesystem::path{};
	}

	ClipboardStatus clipboard_status()
	{
		const auto c = ::platform::clipboard();
		return c ? ClipboardStatus{c->has_drop_files(), c->has_bitmap()} : ClipboardStatus{};
	}

	bool set_clipboard_files(const WindowFramePtr&, std::span<const std::filesystem::path> paths, bool move)
	{
		if (paths.empty()) return false;
		std::vector<core::file_path> files;
		for (const auto& p : paths) files.push_back(desktop::file_path(p));
		return ::platform::set_clipboard(files, {}, {}, move);
	}

	ClipboardContent read_clipboard(const WindowFramePtr& owner)
	{
		ClipboardContent result;
		const auto c = ::platform::clipboard();
		if (!c) return result;
		for (const auto& path : c->drop_paths()) result.files.push_back(::platform::to_stream_path(path));
		if (!result.files.empty())
		{
			result.operation = c->files_description().preferred_drop_effect == ::platform::drop_effect::move
			                       ? FileOperation::move
			                       : FileOperation::copy;
			return result;
		}
		if (auto image = read_clipboard_image(::iw::platform::win32::owner_window(owner)))
			result.image = std::move(*image);
		return result;
	}

	bool perform_file_operation(const WindowFramePtr&, FileOperation op, std::span<const std::filesystem::path> sources,
	                            const std::filesystem::path& destination)
	{
		if (sources.empty()) return false;
		if (op == FileOperation::recycle) return recycle::to_bin(sources);
		std::vector<core::file_path> files;
		for (const auto& p : sources) files.push_back(desktop::file_path(p));
		if (destination.empty()) return false;
		return ::platform::move_or_copy(files, {}, desktop::folder_path(destination), op == FileOperation::move, false)
		    .success();
	}

	namespace
	{
		class DesktopAppHandler final : public AppHandler
		{
		public:
			DesktopAppHandler(::platform::open_with_entry e, std::wstring n) : entry_(std::move(e)), name_(std::move(n))
			{
			}

			const std::wstring& name() const override { return name_; }

			bool invoke(std::span<const std::filesystem::path> paths) const override
			{
				if (paths.empty() || !entry_.invoke) return false;
				std::vector<core::file_path> files;
				for (const auto& p : paths) files.push_back(desktop::file_path(p));
				return entry_.invoke(files, {});
			}

		private:
			::platform::open_with_entry entry_;
			std::wstring name_;
		};

		// An edit inside the one-pixel edge a native edit drew around itself.
		class EditField final : public ::ui::element
		{
		public:
			explicit EditField(::ui::edit_ptr edit) : edit_(std::move(edit)) {}

			void layout(render::measure_context& mc, const core::recti bounds_in) override
			{
				bounds = bounds_in;
				edit_->layout(mc, bounds_in.inflate(-1));
			}

			void render(render::draw_context& dc, const core::pointi element_offset) const override
			{
				dc.draw_rect(bounds.offset(element_offset), core_color(system_color(SystemColor::shadow)));
				render_children(dc, element_offset);
			}

			void visit_children(const std::function<void(const ::ui::element_ptr&)>& visit) const override
			{
				visit(edit_);
			}

		private:
			::ui::edit_ptr edit_;
		};

		class DrawnTextInput final : public TextInput, public std::enable_shared_from_this<DrawnTextInput>
		{
		public:
			explicit DrawnTextInput(TextInputOptions options) : options_(std::move(options)) {}

			void attach(const WindowFramePtr& owner)
			{
				const std::weak_ptr<DrawnTextInput> weak = weak_from_this();
				::ui::edit_styles styles;
				styles.cue = ::core::utf16_to_utf8(options_.cueBanner);
				styles.file_system_auto_complete = options_.folderCompletion;
				if (options_.font && *options_.font)
					styles.font = static_cast<render::font_face>(options_.font->native_handle());
				styles.capture_key_down = [weak](const char32_t key, const ::platform::key_state keys)
				{
					const auto self = weak.lock();
					return self && self->key(key, keys);
				};
				edit_ = ::ui::make_edit(styles, ::core::utf16_to_utf8(options_.text), [weak](const std::string& text)
				{
					const auto self = weak.lock();
					if (!self || !self->options_.changed) return;
					const auto changed = self->options_.changed;
					changed(::core::utf8_to_utf16(text));
				});
				field_ = std::make_shared<EditField>(edit_);
				// Its owner shows it once it has laid the field out.
				field_->is_visible(false);
				element_ = add_frame_element(owner, field_);
			}

			void set_bounds(const app::recti r) override
			{
				if (element_) element_->set_bounds(r);
			}

			void show(const bool v) override
			{
				// A hidden edit keeps no keyboard, or typing would land where nothing shows it.
				if (!v)
					if (const auto input = edit_->input(); input && input->focused() == edit_) input->clear_focus();
				if (element_) element_->show(v);
				else field_->is_visible(v);
			}

			void set_focus() override { edit_->focus(); }

			bool has_focus() const override { return edit_->has_focus(); }

			std::wstring text() const override { return ::core::utf8_to_utf16(edit_->window_text()); }

			void set_text(const std::wstring_view v) override { edit_->window_text(::core::utf16_to_utf8(v)); }

			::ui::control_ptr control() const override { return edit_; }

		private:
			bool key(const char32_t key, const ::platform::key_state keys) const
			{
				if (keys.alt) return false;
				if (key == ::platform::keys::RETURN)
				{
					if (const auto accepted = options_.accepted) accepted();
					return true;
				}
				if (key == ::platform::keys::ESCAPE)
				{
					if (const auto escaped = options_.escaped) escaped();
					return true;
				}
				if (key == ::platform::keys::TAB)
				{
					if (const auto tabbed = options_.tabbed) tabbed(keys.shift);
					return true;
				}
				return false;
			}

			TextInputOptions options_;
			::ui::edit_ptr edit_;
			::ui::element_ptr field_;
			FrameElementPtr element_;
		};
	}

	std::vector<AppHandlerPtr> registered_apps_for_extension(std::wstring_view extension)
	{
		std::vector<AppHandlerPtr> result;
		if (extension.empty() || extension.front() != L'.') return result;
		for (const auto& h : ::platform::assoc_handlers(::core::utf16_to_utf8(extension)))
		{
			result.push_back(std::make_shared<DesktopAppHandler>(h, ::core::utf8_to_utf16(h.name)));
			if (result.size() >= 32) break;
		}
		return result;
	}

	bool launch_process(const std::filesystem::path& executable, std::wstring_view arguments, const WindowFramePtr&)
	{
		if (!executable.is_absolute()) return false;
		// CreateProcess needs argv[0] even when the executable is passed separately.
		const auto commandLine = L"\"" + executable.native() + L"\" " + std::wstring(arguments);
		return ::platform::run(desktop::file_path(executable), ::core::utf16_to_utf8(commandLine));
	}

	TextInputPtr create_text_input(const WindowFramePtr& owner, TextInputOptions options)
	{
		auto input = std::make_shared<DrawnTextInput>(std::move(options));
		input->attach(owner);
		return input;
	}

	Runtime::Runtime(RuntimeMode mode) : mode_(mode)
	{
		initialized_ = mode == RuntimeMode::multithreaded ? SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
		                                                  : SUCCEEDED(OleInitialize(nullptr));
	}

	Runtime::~Runtime()
	{
		if (!initialized_) return;
		if (mode_ == RuntimeMode::multithreaded) CoUninitialize();
		else OleUninitialize();
	}

	void show_error(std::wstring_view message, std::wstring_view title, const WindowFramePtr&)
	{
		::platform::message_box(nullptr, ::core::utf16_to_utf8(message), ::core::utf16_to_utf8(title),
		                        {::platform::message_box_buttons::ok, ::platform::message_box_icon::error});
	}

	void write_diagnostic(std::wstring_view text)
	{
		if (diagnostics::run_log().write(text))
			::core::debug_output(std::string_view{"ImageWalker: diagnostic file write failed."});
		const auto value = ::core::utf16_to_utf8(text);
		::core::debug_output(value);
		::platform::write_stdout(value);
	}

	std::filesystem::path unique_temp_folder(std::wstring_view name)
	{
		const auto folder = ::platform::to_stream_path(::platform::temp_folder());
		GUID id{};
		wchar_t suffix[40]{};
		if (folder.empty() || FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, suffix, ARRAYSIZE(suffix))) return {};
		return folder / std::format(L"{}-{}", name, suffix);
	}

	FileAttributes read_file_attributes(const std::filesystem::path& path)
	{
		FileAttributes r;
		std::error_code e;
		const bool dir = std::filesystem::is_directory(files::native_path(path), e);
		const auto a = dir ? ::platform::file_attributes(desktop::folder_path(path))
		                   : ::platform::file_attributes(desktop::file_path(path));
		if (!a.exists()) return r;
		r.known = true;
		r.hidden = a.is_hidden;
		r.directory = dir;
		r.reparse = a.is_reparse;
		return r;
	}

	bool has_sse2()
	{
		return imaging::has_sse2();
	}

	bool has_avx2()
	{
		return imaging::has_avx2();
	}

	void open_uri(std::wstring_view uri)
	{
		::platform::open(::core::utf16_to_utf8(uri));
	}
}
