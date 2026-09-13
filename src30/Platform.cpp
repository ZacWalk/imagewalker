// ImageWalker by Zac Walker
// Implements Windows runtime, shell, and natural-ordering services.

#include "Platform.h"
#include "Files.h"
#include "PixelOps.h"
#include "Resource.h"
#include "PlatformWin32.h"
#include "DiagnosticLog.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <format>
#include <intrin.h>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace iw::platform
{
	namespace
	{
		HINSTANCE resourceInstance{};
		constexpr UINT messageRunUiWork = WM_APP + 0x3f;

		RECT native_rect(const recti value)
		{
			return {value.x, value.y, value.right(), value.bottom()};
		}

		sizei text_extent(const HDC dc, const std::wstring_view value)
		{
			if (value.empty() || value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return {};
			SIZE extent{};
			return GetTextExtentPoint32W(dc, value.data(), static_cast<int>(value.size()), &extent)
			       ? sizei{extent.cx, extent.cy}
			       : sizei{};
		}
	}

	HWND win32::owner_window(const WindowFramePtr& frame)
	{
		return frame ? reinterpret_cast<HWND>(frame->native_handle()) : nullptr;
	}

	HANDLE win32::shell_small_image_list()
	{
		SHFILEINFOW info{};
		return reinterpret_cast<HANDLE>(SHGetFileInfoW(
			L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
			SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
	}

	int win32::shell_icon_index(const std::filesystem::path& path)
	{
		if (path.empty()) return -1;
		SHFILEINFOW info{};
		return SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
		                      SHGFI_SYSICONINDEX | SHGFI_SMALLICON)
			       ? info.iIcon
			       : -1;
	}

	struct Font::Impl
	{
		HFONT handle{};
		~Impl() { if (handle) DeleteObject(handle); }
	};

	Font::Font() = default;

	Font::Font(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
	{
	}

	Font::~Font() = default;
	Font::Font(Font&&) noexcept = default;
	Font& Font::operator=(Font&&) noexcept = default;
	Font::operator bool() const { return impl_ && impl_->handle; }

	NativeHandle Font::native_handle() const
	{
		return impl_ ? reinterpret_cast<NativeHandle>(impl_->handle) : 0;
	}

	FontPtr create_message_font(const unsigned int dpi)
	{
		NONCLIENTMETRICSW metrics{sizeof(metrics)};
		using SystemParametersInfoForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
		static const auto systemParametersInfoForDpi = reinterpret_cast<SystemParametersInfoForDpiFn>(
			GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));
		const UINT effectiveDpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
		const BOOL loaded = systemParametersInfoForDpi
			                    ? systemParametersInfoForDpi(
				                    SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, effectiveDpi)
			                    : SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
		if (!loaded) return {};
		auto impl = std::make_unique<Font::Impl>();
		impl->handle = CreateFontIndirectW(&metrics.lfMessageFont);
		if (!impl->handle) return {};
		return std::shared_ptr<Font>(new Font(std::move(impl)));
	}

	color system_color(const SystemColor value)
	{
		int index = COLOR_WINDOW;
		switch (value)
		{
		case SystemColor::window: index = COLOR_WINDOW;
			break;
		case SystemColor::windowText: index = COLOR_WINDOWTEXT;
			break;
		case SystemColor::face: index = COLOR_3DFACE;
			break;
		case SystemColor::shadow: index = COLOR_3DSHADOW;
			break;
		case SystemColor::highlight: index = COLOR_HIGHLIGHT;
			break;
		case SystemColor::highlightText: index = COLOR_HIGHLIGHTTEXT;
			break;
		case SystemColor::grayText: index = COLOR_GRAYTEXT;
			break;
		case SystemColor::buttonText: index = COLOR_BTNTEXT;
			break;
		case SystemColor::scrollbar: index = COLOR_SCROLLBAR;
			break;
		}
		return color::from_packed(GetSysColor(index));
	}

	color calc_hande_color(const bool hover, const bool selected, const bool checked)
	{
		if (selected) return system_color(SystemColor::highlight);
		if (hover) return color{188, 218, 246};
		if (checked) return color{214, 228, 242};
		return system_color(SystemColor::face);
	}

	sizei drag_threshold()
	{
		return {GetSystemMetrics(SM_CXDRAG), GetSystemMetrics(SM_CYDRAG)};
	}

	PopupItem PopupItem::action(CommandPtr command, std::wstring text)
	{
		return {std::move(command), std::move(text), false};
	}

	PopupItem PopupItem::separator_item()
	{
		PopupItem result;
		result.separator = true;
		return result;
	}

	void show_popup_menu(const WindowFramePtr& owner, const pointi screenPoint,
	                     const std::span<const PopupItem> items)
	{
		const HMENU menu = CreatePopupMenu();
		if (!menu) return;
		std::vector<CommandPtr> commands;
		for (const auto& item : items)
		{
			if (item.separator)
			{
				AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
				continue;
			}
			if (!item.command) continue;
			commands.push_back(item.command);
			UINT flags = MF_STRING;
			if (item.command->enabled && !item.command->enabled()) flags |= MF_GRAYED;
			if (item.command->checked && item.command->checked()) flags |= MF_CHECKED;
			AppendMenuW(menu, flags, commands.size(), item.label.c_str());
		}
		const UINT selected = TrackPopupMenu(
			menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0,
			win32::owner_window(owner), nullptr);
		DestroyMenu(menu);
		if (selected && selected <= commands.size() && commands[selected - 1]->invoke)
			commands[selected - 1]->invoke();
	}

	class WinMeasureContext final : public MeasureContext
	{
	public:
		explicit WinMeasureContext(const HDC dc) : dc_(dc)
		{
		}

		sizei measure_text(const std::wstring_view value, const std::uint32_t,
		                   const FontPtr& font) const override
		{
			const auto nativeFont = font && *font ? reinterpret_cast<HFONT>(font->native_handle()) : nullptr;
			const HGDIOBJ previousFont = nativeFont ? SelectObject(dc_, nativeFont) : nullptr;
			const sizei result = text_extent(dc_, value);
			if (previousFont) SelectObject(dc_, previousFont);
			return result;
		}

	private:
		HDC dc_{};
	};

	class WinDrawContext final : public DrawContext
	{
	public:
		WinDrawContext(const HDC target, const recti clip, const int width, const int height)
			: target_(target), clip_(clip), width_(width), height_(height)
		{
			if (width_ <= 0 || height_ <= 0) return;
			dc_ = CreateCompatibleDC(target_);
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = width_;
			info.bmiHeader.biHeight = -height_;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			bitmap_ = CreateDIBSection(target_, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
			if (!dc_ || !bitmap_)
			{
				if (bitmap_) DeleteObject(bitmap_);
				if (dc_) DeleteDC(dc_);
				bitmap_ = nullptr;
				dc_ = target_;
				pixels_ = nullptr;
				return;
			}
			oldBitmap_ = SelectObject(dc_, bitmap_);
		}

		~WinDrawContext() override
		{
			if (dc_ != target_)
			{
				SelectObject(dc_, oldBitmap_);
				DeleteObject(bitmap_);
				DeleteDC(dc_);
			}
			for (const auto& [value, brush] : brushes_) DeleteObject(brush);
		}

		void begin(const HDC target, const recti clip)
		{
			target_ = target;
			clip_ = clip;
			SelectClipRgn(dc_, nullptr);
		}

		bool reusable(const int width, const int height) const
		{
			return dc_ != target_ && width_ == width && height_ == height;
		}

		void present() const
		{
			if (dc_ != target_) BitBlt(target_, 0, 0, width_, height_, dc_, 0, 0, SRCCOPY);
		}

		recti clip_rect() const override { return clip_; }

		void fill(const recti bounds, const color value) override
		{
			const RECT area = native_rect(bounds);
			FillRect(dc_, &area, brush(value));
		}

		void blend_fill(const recti bounds, const color value) override
		{
			if (!pixels_ || value.a == 255)
			{
				fill(bounds, value);
				return;
			}
			GdiFlush();
			const int left = std::clamp(bounds.x, 0, width_);
			const int right = std::clamp(bounds.right(), 0, width_);
			const int top = std::clamp(bounds.y, 0, height_);
			const int bottom = std::clamp(bounds.bottom(), 0, height_);
			// recti carries no ordering invariant, and an inverted rect would size the span from a
			// negative difference. FillRect tolerates one, so the two paths have to agree here.
			const auto count = static_cast<size_t>((std::max)(0, right - left));
			for (int y = top; y < bottom; ++y)
				ui::blend_constant_bgra(
					{pixels_ + static_cast<size_t>(y) * width_ + left, count}, value);
		}

		void text(const std::wstring_view value, const recti bounds, const color textColor,
		          const std::uint32_t format, const FontPtr& font) override
		{
			if (value.empty() || bounds.width <= 0 || bounds.height <= 0 ||
				value.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
				return;
			const auto nativeFont = font && *font ? reinterpret_cast<HFONT>(font->native_handle()) : nullptr;
			const HGDIOBJ previousFont = nativeFont ? SelectObject(dc_, nativeFont) : nullptr;
			const int previousMode = SetBkMode(dc_, TRANSPARENT);
			const COLORREF previousColor = SetTextColor(dc_, textColor.pack());

			// DrawTextW was considered too expensive for this retained-canvas hot path.
			std::wstring shortened;
			std::wstring_view displayed = value;
			sizei extent = text_extent(dc_, displayed);
			if ((format & TextFormat::endEllipsis) && extent.width > bounds.width)
			{
				constexpr std::wstring_view ellipsis = L"...";
				const int prefixWidth = (std::max)(0, bounds.width - text_extent(dc_, ellipsis).width);
				int fit{};
				SIZE fittedExtent{};
				GetTextExtentExPointW(dc_, value.data(), static_cast<int>(value.size()), prefixWidth,
				                       &fit, nullptr, &fittedExtent);
				if (fit > 0 && fit < static_cast<int>(value.size()) &&
					value[fit - 1] >= 0xd800 && value[fit - 1] <= 0xdbff &&
					value[fit] >= 0xdc00 && value[fit] <= 0xdfff)
					--fit;
				shortened.assign(value.substr(0, static_cast<size_t>(fit)));
				shortened.append(ellipsis);
				displayed = shortened;
				extent = text_extent(dc_, displayed);
			}

			int x = bounds.x;
			if (format & TextFormat::center) x += (bounds.width - extent.width) / 2;
			else if (format & TextFormat::right) x += bounds.width - extent.width;
			int y = bounds.y;
			if (format & TextFormat::verticalCenter) y += (bounds.height - extent.height) / 2;
			const RECT area = native_rect(bounds);
			ExtTextOutW(dc_, x, y, ETO_CLIPPED, &area, displayed.data(), static_cast<UINT>(displayed.size()), nullptr);
			SetTextColor(dc_, previousColor);
			SetBkMode(dc_, previousMode);
			if (previousFont) SelectObject(dc_, previousFont);
		}

		sizei measure_text(const std::wstring_view value, const std::uint32_t format,
		                   const FontPtr& font) override
		{
			return WinMeasureContext(dc_).measure_text(value, format, font);
		}

		void image(const std::span<const std::uint32_t> sourcePixels, const sizei source,
		           const recti destination, const bool alpha) override
		{
			if (source.width <= 0 || source.height <= 0 || sourcePixels.empty() ||
				destination.width <= 0 || destination.height <= 0)
				return;
			const size_t sourceWidth = static_cast<size_t>(source.width);
			const size_t sourceHeight = static_cast<size_t>(source.height);
			if (sourceWidth > (std::numeric_limits<size_t>::max)() / sourceHeight ||
				sourcePixels.size() < sourceWidth * sourceHeight)
				return;
			if (alpha && pixels_)
			{
				GdiFlush();
				const int startX = (std::max)(0, destination.x);
				const int endX = (std::min)(width_, destination.right());
				const int startY = (std::max)(0, destination.y);
				const int endY = (std::min)(height_, destination.bottom());
				for (int y = startY; y < endY; ++y)
				{
					const int sourceY = std::clamp(static_cast<int>(
						static_cast<std::int64_t>(y - destination.y) * source.height / destination.height),
					                               0, source.height - 1);
					for (int x = startX; x < endX; ++x)
					{
						const int sourceX = std::clamp(static_cast<int>(
							static_cast<std::int64_t>(x - destination.x) * source.width / destination.width),
						                               0, source.width - 1);
						const auto sourcePixel = sourcePixels[static_cast<size_t>(sourceY) * source.width + sourceX];
						const int sourceAlpha = static_cast<int>((sourcePixel >> 24) & 0xff);
						if (!sourceAlpha) continue;
						auto& pixel = pixels_[static_cast<size_t>(y) * width_ + x];
						const auto over = [sourceAlpha](const std::uint32_t background,
						                                const std::uint32_t foreground)
						{
							return (background * (255 - sourceAlpha) + foreground * sourceAlpha + 127) / 255;
						};
						pixel = over(pixel & 0xff, sourcePixel & 0xff) |
							(over((pixel >> 8) & 0xff, (sourcePixel >> 8) & 0xff) << 8) |
							(over((pixel >> 16) & 0xff, (sourcePixel >> 16) & 0xff) << 16) | 0xff000000;
					}
				}
				return;
			}
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = source.width;
			info.bmiHeader.biHeight = -source.height;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			const int previousMode = SetStretchBltMode(dc_, HALFTONE);
			SetBrushOrgEx(dc_, 0, 0, nullptr);
			StretchDIBits(dc_, destination.x, destination.y, destination.width, destination.height,
			              0, 0, source.width, source.height, sourcePixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
			SetStretchBltMode(dc_, previousMode);
		}

		void focus(const recti bounds) override
		{
			const RECT area = native_rect(bounds);
			DrawFocusRect(dc_, &area);
		}

		void outline(const recti bounds, const color value, const int width) override
		{
			const HPEN pen = CreatePen(PS_SOLID, (std::max)(1, width), value.pack());
			if (!pen) return;
			const HGDIOBJ oldPen = SelectObject(dc_, pen);
			const HGDIOBJ oldBrush = SelectObject(dc_, GetStockObject(NULL_BRUSH));
			Rectangle(dc_, bounds.x, bounds.y, bounds.right(), bounds.bottom());
			SelectObject(dc_, oldBrush);
			SelectObject(dc_, oldPen);
			DeleteObject(pen);
		}

		void clip(const recti bounds) override
		{
			IntersectClipRect(dc_, bounds.x, bounds.y, bounds.right(), bounds.bottom());
		}

		void reset_clip() override { SelectClipRgn(dc_, nullptr); }

	private:
		HBRUSH brush(const color value)
		{
			for (const auto& [cached, handle] : brushes_) if (cached == value) return handle;
			const HBRUSH created = CreateSolidBrush(value.pack());
			if (created) brushes_.emplace_back(value, created);
			return created;
		}

		HDC target_{};
		HDC dc_{};
		HBITMAP bitmap_{};
		HGDIOBJ oldBitmap_{};
		std::uint32_t* pixels_{};
		recti clip_;
		int width_{};
		int height_{};
		std::vector<std::pair<color, HBRUSH>> brushes_;
	};

	class WinWindowFrame final : public WindowFrame, public std::enable_shared_from_this<WinWindowFrame>
	{
	public:
		~WinWindowFrame() override
		{
			if (accelerators_) DestroyAcceleratorTable(accelerators_);
			if (window_ && IsWindow(window_)) SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
		}

		static std::shared_ptr<WinWindowFrame> create(const HWND parent, FrameReactorPtr reactor,
		                                              const WindowOptions& options)
		{
			auto frame = std::make_shared<WinWindowFrame>();
			frame->reactor_ = std::move(reactor);
			frame->self_ = frame;
			frame->topLevel_ = parent == nullptr;
			frame->eraseBackground_ = options.eraseBackground;
			const std::wstring className(options.className.begin(), options.className.end());
			WNDCLASSEXW windowClass{sizeof(windowClass)};
			windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
			windowClass.lpfnWndProc = window_proc;
			windowClass.hInstance = resourceInstance;
			windowClass.hIcon = options.icon == WindowIcon::application
				                    ? LoadIconW(resourceInstance, MAKEINTRESOURCEW(IDI_APP))
				                    : nullptr;
			windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			windowClass.hbrBackground = options.eraseBackground
				                              ? reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)
				                              : nullptr;
			windowClass.lpszClassName = className.c_str();
			windowClass.hIconSm = windowClass.hIcon;
			if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return {};
			DWORD style = options.child ? WS_CHILD : WS_OVERLAPPEDWINDOW;
			if (options.visible) style |= WS_VISIBLE;
			if (options.clipChildren) style |= WS_CLIPCHILDREN;
			if (options.tabStop) style |= WS_TABSTOP;
			const int x = options.useDefaultPosition ? CW_USEDEFAULT : options.bounds.x;
			const int y = options.useDefaultPosition ? CW_USEDEFAULT : options.bounds.y;
			const int width = options.bounds.width;
			const int height = options.bounds.height;
			frame->window_ = CreateWindowExW(0, className.c_str(), options.title.c_str(), style,
			                                 x, y, width, height, parent, nullptr, resourceInstance, frame.get());
			if (!frame->window_)
			{
				frame->self_.reset();
				return {};
			}
			if (!options.child && options.showCommand)
			{
				ShowWindow(frame->window_, options.maximized ? SW_SHOWMAXIMIZED : options.showCommand);
				UpdateWindow(frame->window_);
			}
			return frame;
		}

		NativeHandle native_handle() const override { return reinterpret_cast<NativeHandle>(window_); }
		void set_reactor(FrameReactorPtr reactor) override { reactor_ = std::move(reactor); }

		WindowFramePtr create_child(FrameReactorPtr reactor, const WindowOptions& options) override
		{
			return create(window_, std::move(reactor), options);
		}

		recti client_rect() const override
		{
			RECT value{};
			GetClientRect(window_, &value);
			return {value.left, value.top, value.right - value.left, value.bottom - value.top};
		}

		void move(const recti bounds) override
		{
			MoveWindow(window_, bounds.x, bounds.y, bounds.width, bounds.height, TRUE);
		}

		void show(const bool visible) override { ShowWindow(window_, visible ? SW_SHOW : SW_HIDE); }
		void invalidate() override { InvalidateRect(window_, nullptr, FALSE); }

		void invalidate(const recti bounds) override
		{
			const RECT area = native_rect(bounds);
			InvalidateRect(window_, &area, FALSE);
		}

		void set_focus() override { SetFocus(window_); }
		bool has_focus() const override { return GetFocus() == window_; }
		void set_capture() override { SetCapture(window_); }
		void release_capture() override { if (GetCapture() == window_) ReleaseCapture(); }
		void start_timer(const unsigned int milliseconds) override
		{
			SetTimer(window_, 1, (std::max)(1u, milliseconds), nullptr);
		}
		void stop_timer() override { KillTimer(window_, 1); }

		void track_mouse_leave() override
		{
			TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
			TrackMouseEvent(&tracking);
		}

		void configure_gestures(const bool zoom, const bool pan) override
		{
			GESTURECONFIG gestures[]{
				{
					GID_ZOOM, zoom ? static_cast<DWORD>(GC_ZOOM) : 0u,
					zoom ? 0u : static_cast<DWORD>(GC_ZOOM)
				},
				{
					GID_PAN, pan ? static_cast<DWORD>(GC_PAN) : 0u,
					pan ? 0u : static_cast<DWORD>(GC_PAN)
				}
			};
			SetGestureConfig(window_, 0, std::size(gestures), gestures, sizeof(GESTURECONFIG));
		}

		void accept_file_drops(const bool accept) override
		{
			if (window_) DragAcceptFiles(window_, accept);
		}

		void set_cursor(const CursorShape cursor) override
		{
			LPCWSTR identifier = IDC_ARROW;
			if (cursor == CursorShape::sizeAll) identifier = IDC_SIZEALL;
			else if (cursor == CursorShape::sizeHorizontal) identifier = IDC_SIZEWE;
			else if (cursor == CursorShape::wait) identifier = IDC_WAIT;
			if (cursor == CursorShape::zoom)
				SetCursor(LoadCursorW(resourceInstance, MAKEINTRESOURCEW(IDC_ZOOM_CURSOR)));
			else SetCursor(LoadCursorW(nullptr, identifier));
		}

		pointi screen_to_client(const pointi point) const override
		{
			POINT value{point.x, point.y};
			ScreenToClient(window_, &value);
			return {value.x, value.y};
		}

		pointi client_to_screen(const pointi point) const override
		{
			POINT value{point.x, point.y};
			ClientToScreen(window_, &value);
			return {value.x, value.y};
		}

		void set_title(const std::wstring_view title) override
		{
			const std::wstring value(title);
			SetWindowTextW(window_, value.c_str());
		}

		void set_fullscreen(const bool fullscreen) override
		{
			if (!topLevel_ || fullscreen == fullscreen_) return;
			fullscreen_ = fullscreen;
			if (fullscreen)
			{
				normalStyle_ = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
				normalPlacement_ = {sizeof(normalPlacement_)};
				GetWindowPlacement(window_, &normalPlacement_);
				MONITORINFO monitor{sizeof(monitor)};
				GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
				SetWindowLongPtrW(window_, GWL_STYLE, normalStyle_ & ~WS_OVERLAPPEDWINDOW);
				SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
				             monitor.rcMonitor.right - monitor.rcMonitor.left,
				             monitor.rcMonitor.bottom - monitor.rcMonitor.top,
				             SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
			}
			else
			{
				SetWindowLongPtrW(window_, GWL_STYLE, normalStyle_);
				SetWindowPlacement(window_, &normalPlacement_);
				SetWindowPos(window_, nullptr, 0, 0, 0, 0,
				             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
			}
		}

		void set_maximized(const bool maximized) override
		{
			if (fullscreen_) set_fullscreen(false);
			ShowWindow(window_, maximized ? SW_MAXIMIZE : SW_RESTORE);
		}

		WindowPlacement placement() const override
		{
			WINDOWPLACEMENT value{sizeof(value)};
			if (fullscreen_ && normalPlacement_.length) value = normalPlacement_;
			else if (!GetWindowPlacement(window_, &value)) return {};
			return {
				{
					value.rcNormalPosition.left, value.rcNormalPosition.top,
					value.rcNormalPosition.right - value.rcNormalPosition.left,
					value.rcNormalPosition.bottom - value.rcNormalPosition.top
				},
				value.showCmd == SW_SHOWMAXIMIZED
			};
		}

		void set_accelerators(const std::span<const CommandAccelerator> accelerators) override
		{
			if (accelerators_)
			{
				DestroyAcceleratorTable(accelerators_);
				accelerators_ = nullptr;
			}
			std::vector<ACCEL> native;
			native.reserve(accelerators.size());
			for (const auto& accelerator : accelerators)
			{
				WORD key{};
				switch (accelerator.shortcut.key)
				{
				case ShortcutKey::a: key = L'A'; break;
				case ShortcutKey::b: key = L'B'; break;
				case ShortcutKey::c: key = L'C'; break;
				case ShortcutKey::e: key = L'E'; break;
				case ShortcutKey::n: key = L'N'; break;
				case ShortcutKey::o: key = L'O'; break;
				case ShortcutKey::r: key = L'R'; break;
				case ShortcutKey::v: key = L'V'; break;
				case ShortcutKey::x: key = L'X'; break;
				case ShortcutKey::z: key = L'Z'; break;
				case ShortcutKey::deleteKey: key = VK_DELETE; break;
				case ShortcutKey::enterKey: key = VK_RETURN; break;
				case ShortcutKey::f1: key = VK_F1; break;
				case ShortcutKey::f2: key = VK_F2; break;
				case ShortcutKey::f3: key = VK_F3; break;
				case ShortcutKey::f4: key = VK_F4; break;
				case ShortcutKey::f5: key = VK_F5; break;
				case ShortcutKey::f6: key = VK_F6; break;
				case ShortcutKey::f7: key = VK_F7; break;
				case ShortcutKey::f8: key = VK_F8; break;
				case ShortcutKey::f9: key = VK_F9; break;
				case ShortcutKey::f11: key = VK_F11; break;
				case ShortcutKey::f12: key = VK_F12; break;
				default: break;
				}
				if (!key || !accelerator.command) continue;
				BYTE flags = FVIRTKEY;
				if (accelerator.shortcut.control) flags |= FCONTROL;
				if (accelerator.shortcut.shift) flags |= FSHIFT;
				if (accelerator.shortcut.alt) flags |= FALT;
				native.push_back({flags, key, accelerator.command});
			}
			if (!native.empty())
				accelerators_ = CreateAcceleratorTableW(native.data(), static_cast<int>(native.size()));
		}

		bool translate_accelerator(MSG& message) const
		{
			wchar_t className[32]{};
			if (GetClassNameW(message.hwnd, className, static_cast<int>(std::size(className))) &&
				_wcsicmp(className, L"EDIT") == 0)
				return false;
			return accelerators_ && TranslateAcceleratorW(window_, accelerators_, &message);
		}

		void close() override { DestroyWindow(window_); }

		unsigned int dpi() const override
		{
			using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
			static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			if (getDpiForWindow) return getDpiForWindow(window_);
			const HDC dc = GetDC(window_);
			const UINT value = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : USER_DEFAULT_SCREEN_DPI;
			if (dc) ReleaseDC(window_, dc);
			return value;
		}

	private:
		static pointi message_point(const LPARAM value)
		{
			return {static_cast<short>(LOWORD(value)), static_cast<short>(HIWORD(value))};
		}

		static KeyCode key_code(const WPARAM value)
		{
			switch (value)
			{
			case VK_LEFT: return KeyCode::left;
			case VK_RIGHT: return KeyCode::right;
			case VK_UP: return KeyCode::up;
			case VK_DOWN: return KeyCode::down;
			case VK_HOME: return KeyCode::home;
			case VK_END: return KeyCode::end;
			case VK_PRIOR: return KeyCode::pageUp;
			case VK_NEXT: return KeyCode::pageDown;
			case VK_SPACE: return KeyCode::space;
			case VK_DELETE: return KeyCode::deleteKey;
			case VK_F2: return KeyCode::f2;
			case VK_RETURN: return KeyCode::enter;
			case VK_ESCAPE: return KeyCode::escape;
			case VK_DIVIDE:
			case VK_OEM_2: return KeyCode::divide;
			case VK_MULTIPLY: return KeyCode::multiply;
			case VK_ADD:
			case VK_OEM_PLUS: return KeyCode::add;
			case VK_SUBTRACT:
			case VK_OEM_MINUS: return KeyCode::subtract;
			default: return KeyCode::unknown;
			}
		}

		static LRESULT CALLBACK window_proc(const HWND window, const UINT message,
		                                    const WPARAM wparam, const LPARAM lparam) noexcept
		{
			try
			{
				auto* frame = reinterpret_cast<WinWindowFrame*>(GetWindowLongPtrW(window, GWLP_USERDATA));
				if (message == WM_NCCREATE)
				{
					frame = static_cast<WinWindowFrame*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
					frame->window_ = window;
					SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(frame));
				}
				return frame ? frame->dispatch(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
			}
			catch (...) { return DefWindowProcW(window, message, wparam, lparam); }
		}

		LRESULT dispatch(const UINT message, const WPARAM wparam, const LPARAM lparam)
		{
			const auto self = self_;
			if (!self) return DefWindowProcW(window_, message, wparam, lparam);
			if (message == WM_NCDESTROY)
			{
				const HWND window = window_;
				SetWindowLongPtrW(window, GWLP_USERDATA, 0);
				window_ = nullptr;
				self_.reset();
				return DefWindowProcW(window, message, wparam, lparam);
			}
			if (!reactor_) return DefWindowProcW(window_, message, wparam, lparam);
			if (message == WM_ERASEBKGND && !eraseBackground_) return 1;
			if (message == WM_PAINT)
			{
				PAINTSTRUCT paint{};
				const HDC dc = BeginPaint(window_, &paint);
				RECT client{};
				GetClientRect(window_, &client);
				const recti clip{
					paint.rcPaint.left, paint.rcPaint.top,
					paint.rcPaint.right - paint.rcPaint.left,
					paint.rcPaint.bottom - paint.rcPaint.top
				};
				const int width = client.right - client.left;
				const int height = client.bottom - client.top;
				if (!drawContext_ || !drawContext_->reusable(width, height))
					drawContext_ = std::make_unique<WinDrawContext>(dc, clip, width, height);
				else drawContext_->begin(dc, clip);
				try
				{
					reactor_->paint(self, *drawContext_);
					drawContext_->present();
				}
				catch (...)
				{
					EndPaint(window_, &paint);
					throw;
				}
				EndPaint(window_, &paint);
				return 0;
			}
			if (message == WM_SIZE)
			{
				const HDC dc = GetDC(window_);
				WinMeasureContext context(dc);
				try { reactor_->size(self, {LOWORD(lparam), HIWORD(lparam)}, context); }
				catch (...)
				{
					ReleaseDC(window_, dc);
					throw;
				}
				ReleaseDC(window_, dc);
				return 0;
			}
			if (message == WM_DPICHANGED)
			{
				const auto* suggested = reinterpret_cast<const RECT*>(lparam);
				SetWindowPos(window_, nullptr, suggested->left, suggested->top,
				             suggested->right - suggested->left, suggested->bottom - suggested->top,
				             SWP_NOZORDER | SWP_NOACTIVATE);
				return reactor_->message(self, WindowMessage::dpiChanged);
			}
			switch (message)
			{
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_MOUSEMOVE:
			case WM_MOUSELEAVE:
			case WM_MOUSEWHEEL:
			case WM_CONTEXTMENU:
				return dispatch_mouse(self, message, wparam, lparam);
			default: break;
			}
			if (message == WM_KEYDOWN || message == WM_CHAR)
			{
				const wchar_t character = message == WM_KEYDOWN &&
				                          ((wparam >= L'0' && wparam <= L'9') || (wparam >= L'A' && wparam <= L'Z'))
					                          ? static_cast<wchar_t>(wparam)
					                          : message == WM_CHAR
					                          ? static_cast<wchar_t>(wparam)
					                          : L'\0';
				const KeyInput input{
					key_code(wparam), character,
					(GetKeyState(VK_CONTROL) & 0x8000) != 0,
					(GetKeyState(VK_SHIFT) & 0x8000) != 0,
					(GetKeyState(VK_MENU) & 0x8000) != 0
				};
				return reactor_->key(self, message == WM_KEYDOWN ? KeyMessage::down : KeyMessage::character, input);
			}
			if (message == WM_GESTURE)
			{
				GESTUREINFO gesture{sizeof(gesture)};
				if (GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lparam), &gesture))
				{
					POINT location{gesture.ptsLocation.x, gesture.ptsLocation.y};
					ScreenToClient(window_, &location);
					const GestureInput input{
						gesture.dwID == GID_PAN ? GestureKind::pan : GestureKind::zoom,
						{location.x, location.y}, gesture.ullArguments,
						(gesture.dwFlags & GF_BEGIN) != 0, (gesture.dwFlags & GF_END) != 0
					};
					MessageResult result{};
					try { result = reactor_->gesture(self, input); }
					catch (...)
					{
						CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lparam));
						throw;
					}
					CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lparam));
					return result;
				}
				CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lparam));
				return 0;
			}
			if (message == WM_DROPFILES)
			{
				const auto drop = reinterpret_cast<HDROP>(wparam);
				POINT location{};
				DragQueryPoint(drop, &location);
				const FileDrop dropped{win32::dropped_files(drop), {location.x, location.y}};
				DragFinish(drop);
				return reactor_->files_dropped(self, dropped);
			}
			std::optional<WindowMessage> mapped;
			switch (message)
			{
			case WM_CREATE: mapped = WindowMessage::create;
				break;
			case WM_DESTROY: mapped = WindowMessage::destroy;
				break;
			case WM_CLOSE: mapped = WindowMessage::close;
				break;
			case WM_SETFOCUS: mapped = WindowMessage::focusGained;
				break;
			case WM_KILLFOCUS: mapped = WindowMessage::focusLost;
				break;
			case WM_CAPTURECHANGED: mapped = WindowMessage::captureLost;
				break;
			case WM_TIMER: mapped = WindowMessage::timer;
				break;
			default: break;
			}
			if (mapped)
			{
				const auto result = reactor_->message(self, *mapped);
				if (message == WM_DESTROY && topLevel_) PostQuitMessage(0);
				return result;
			}
			return DefWindowProcW(window_, message, wparam, lparam);
		}

		MessageResult dispatch_mouse(const WindowFramePtr& self, const UINT message,
		                             const WPARAM wparam, const LPARAM lparam)
		{
			const pointi messagePoint = message_point(lparam);
			pointi clientPoint = messagePoint;
			pointi screenPoint = messagePoint;
			if (message == WM_MOUSEWHEEL || message == WM_CONTEXTMENU)
			{
				if (message == WM_CONTEXTMENU && messagePoint.x == -1 && messagePoint.y == -1)
				{
					POINT cursor{};
					GetCursorPos(&cursor);
					screenPoint = {cursor.x, cursor.y};
				}
				POINT point{screenPoint.x, screenPoint.y};
				ScreenToClient(window_, &point);
				clientPoint = {point.x, point.y};
			}
			else
			{
				POINT point{clientPoint.x, clientPoint.y};
				ClientToScreen(window_, &point);
				screenPoint = {point.x, point.y};
			}
			MouseInput mouseInput{
				clientPoint, screenPoint, (wparam & MK_LBUTTON) != 0,
				(wparam & MK_CONTROL) != 0, (wparam & MK_SHIFT) != 0, 0
			};
			switch (message)
			{
			case WM_LBUTTONDOWN: return reactor_->mouse(self, MouseMessage::leftButtonDown, mouseInput);
			case WM_LBUTTONDBLCLK: return reactor_->mouse(self, MouseMessage::leftButtonDoubleClick, mouseInput);
			case WM_RBUTTONDOWN: return reactor_->mouse(self, MouseMessage::rightButtonDown, mouseInput);
			case WM_LBUTTONUP: return reactor_->mouse(self, MouseMessage::leftButtonUp, mouseInput);
			case WM_MOUSEMOVE: return reactor_->mouse(self, MouseMessage::move, mouseInput);
			case WM_MOUSELEAVE: return reactor_->mouse(self, MouseMessage::leave, mouseInput);
			case WM_MOUSEWHEEL:
				mouseInput.wheelDelta = static_cast<short>(HIWORD(wparam));
				return reactor_->mouse(self, MouseMessage::wheel, mouseInput);
			default: return reactor_->mouse(self, MouseMessage::contextMenu, mouseInput);
			}
		}

		HWND window_{};
		std::unique_ptr<WinDrawContext> drawContext_;
		FrameReactorPtr reactor_;
		WindowFramePtr self_;
		WINDOWPLACEMENT normalPlacement_{};
		DWORD normalStyle_{};
		bool eraseBackground_{true};
		bool topLevel_{};
		bool fullscreen_{};
		HACCEL accelerators_{};
	};

	WindowFramePtr create_top_level_frame(FrameReactorPtr reactor, const WindowOptions& options)
	{
		return WinWindowFrame::create(nullptr, std::move(reactor), options);
	}

	BitmapResource load_bitmap_resource(const BitmapAsset asset)
	{
		BitmapResource result;
		const int resource = [asset]
		{
			switch (asset)
			{
			case BitmapAsset::fileFolder: return IDB_FILE_FOLDER;
			case BitmapAsset::filePhoto: return IDB_FILE_PHOTO;
			case BitmapAsset::fileDocument: return IDB_FILE_DOCUMENT;
			case BitmapAsset::fileVideo: return IDB_FILE_VIDEO;
			case BitmapAsset::fileAudio: return IDB_FILE_AUDIO;
			case BitmapAsset::fileOther: return IDB_FILE_OTHER;
			case BitmapAsset::toolbarSymbols: return IDB_TOOLBAR_SYMBOLS;
			default: return IDB_STATUS_SYMBOLS;
			}
		}();
		const auto bitmap = static_cast<HBITMAP>(LoadImageW(resourceInstance, MAKEINTRESOURCEW(resource), IMAGE_BITMAP,
		                                                    0, 0, LR_CREATEDIBSECTION));
		if (!bitmap) return result;
		BITMAP value{};
		if (GetObjectW(bitmap, sizeof(value), &value) == sizeof(value) && value.bmWidth > 0 && value.bmHeight != 0)
		{
			result.size = {value.bmWidth, std::abs(value.bmHeight)};
			result.pixels.resize(static_cast<size_t>(result.size.width) * result.size.height);
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = result.size.width;
			info.bmiHeader.biHeight = -result.size.height;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			const HDC dc = GetDC(nullptr);
			if (!dc || GetDIBits(dc, bitmap, 0, result.size.height, result.pixels.data(), &info, DIB_RGB_COLORS) !=
				result.size.height)
				result = {};
			if (dc) ReleaseDC(nullptr, dc);
		}
		DeleteObject(bitmap);
		const bool missingAlpha = std::ranges::none_of(result.pixels, [](const std::uint32_t pixel)
		{
			return (pixel & 0xff000000) != 0;
		});
		if (missingAlpha) for (auto& pixel : result.pixels) pixel |= 0xff000000;
		else
			result.hasAlpha = std::ranges::any_of(result.pixels, [](const std::uint32_t pixel)
			{
				return (pixel & 0xff000000) != 0xff000000;
			});
		return result;
	}

	namespace
	{
		std::optional<std::filesystem::path> shell_item_path(IShellItem* item)
		{
			PWSTR value = nullptr;
			if (!item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) return std::nullopt;
			std::filesystem::path result(value);
			CoTaskMemFree(value);
			return result;
		}

		std::wstring path_list(const std::span<const std::filesystem::path> paths)
		{
			std::wstring result;
			for (const auto& path : paths)
			{
				if (path.empty()) continue;
				result += path.wstring();
				result.push_back(L'\0');
			}
			if (!result.empty()) result.push_back(L'\0');
			return result;
		}

		const std::filesystem::path& settings_path()
		{
			static const std::filesystem::path path = []
			{
				std::wstring modulePath(MAX_PATH, L'\0');
				for (;;)
				{
					const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(),
					                                        static_cast<DWORD>(modulePath.size()));
					if (!length) return std::filesystem::path(L"imagewalker30.ini");
					if (length < modulePath.size())
					{
						modulePath.resize(length);
						auto destination = std::filesystem::path(modulePath);
						destination.replace_extension(L".ini");
						const auto legacy = destination.parent_path() / L"imagewalker.ini";
						if (GetFileAttributesW(destination.c_str()) == INVALID_FILE_ATTRIBUTES &&
							GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES &&
							!CopyFileW(legacy.c_str(), destination.c_str(), TRUE) &&
							GetLastError() != ERROR_FILE_EXISTS)
							write_diagnostic(L"Unable to migrate legacy ImageWalker settings.\n");
						return destination;
					}
					if (modulePath.size() >= 32768) return std::filesystem::path(L"imagewalker30.ini");
					modulePath.resize(modulePath.size() * 2);
				}
			}();
			return path;
		}

		HBITMAP bitmap_from_dib(const HGLOBAL data)
		{
			if (!data) return nullptr;
			const SIZE_T bytes = GlobalSize(data);
			const auto* memory = static_cast<const BYTE*>(GlobalLock(data));
			if (!memory || bytes < sizeof(BITMAPINFOHEADER))
			{
				if (memory) GlobalUnlock(data);
				return nullptr;
			}
			const auto* header = reinterpret_cast<const BITMAPINFOHEADER*>(memory);
			size_t pixelOffset = header->biSize;
			if (header->biSize < sizeof(BITMAPINFOHEADER) || pixelOffset > bytes || header->biWidth <= 0 ||
				header->biHeight == 0 || header->biHeight == (std::numeric_limits<LONG>::min)() ||
				header->biPlanes != 1 ||
				(header->biBitCount != 1 && header->biBitCount != 4 && header->biBitCount != 8 &&
				 header->biBitCount != 16 && header->biBitCount != 24 && header->biBitCount != 32) ||
				(header->biCompression != BI_RGB && header->biCompression != BI_BITFIELDS) ||
				(header->biCompression == BI_BITFIELDS && header->biBitCount != 16 && header->biBitCount != 32))
			{
				GlobalUnlock(data);
				return nullptr;
			}
			const size_t masks = header->biSize == sizeof(BITMAPINFOHEADER) &&
			                     header->biCompression == BI_BITFIELDS ? 3 * sizeof(DWORD) : 0;
			const size_t colors = header->biClrUsed
				                      ? header->biClrUsed
				                      : header->biBitCount <= 8
				                      ? size_t{1} << header->biBitCount
				                      : 0;
			if (masks > bytes - pixelOffset || colors > (bytes - pixelOffset - masks) / sizeof(RGBQUAD))
			{
				GlobalUnlock(data);
				return nullptr;
			}
			pixelOffset += masks + colors * sizeof(RGBQUAD);
			const size_t width = static_cast<size_t>(header->biWidth);
			const size_t height = static_cast<size_t>(std::abs(static_cast<std::int64_t>(header->biHeight)));
			if (width > ((std::numeric_limits<size_t>::max)() - 31) / header->biBitCount)
			{
				GlobalUnlock(data);
				return nullptr;
			}
			const size_t stride = ((width * header->biBitCount + 31) / 32) * 4;
			if (height > (bytes - pixelOffset) / stride)
			{
				GlobalUnlock(data);
				return nullptr;
			}
			HBITMAP bitmap = nullptr;
			if (pixelOffset < bytes)
			{
				const HDC dc = GetDC(nullptr);
				if (dc)
				{
					bitmap = CreateDIBitmap(dc, header, CBM_INIT, memory + pixelOffset,
					                        reinterpret_cast<const BITMAPINFO*>(memory), DIB_RGB_COLORS);
					ReleaseDC(nullptr, dc);
				}
			}
			GlobalUnlock(data);
			return bitmap;
		}

		BitmapResource bitmap_resource(const HBITMAP bitmap)
		{
			BitmapResource result;
			BITMAP description{};
			if (!bitmap || GetObjectW(bitmap, sizeof(description), &description) != sizeof(description) ||
				description.bmWidth <= 0 || description.bmHeight == 0)
				return result;
			result.size = {description.bmWidth, std::abs(description.bmHeight)};
			const auto bytes = static_cast<std::uint64_t>(result.size.width) * result.size.height * 4;
			if (bytes > 256ull * 1024 * 1024) return {};
			result.pixels.resize(static_cast<size_t>(result.size.width) * result.size.height);
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = result.size.width;
			info.bmiHeader.biHeight = -result.size.height;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			const HDC dc = GetDC(nullptr);
			const int lines = dc
				                  ? GetDIBits(dc, bitmap, 0, result.size.height, result.pixels.data(), &info,
				                              DIB_RGB_COLORS)
				                  : 0;
			if (dc) ReleaseDC(nullptr, dc);
			if (lines != result.size.height) return {};
			result.hasAlpha = std::ranges::any_of(result.pixels, [](const std::uint32_t pixel)
			{
				return (pixel & 0xff000000) != 0;
			});
			if (!result.hasAlpha) for (auto& pixel : result.pixels) pixel |= 0xff000000;
			return result;
		}
	}

	namespace
	{
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
					try { work(); }
					catch (...) {}
				}
			}

			std::mutex mutex_;
			std::condition_variable ready_;
			std::deque<std::function<void()>> queue_;
			std::thread thread_;
			bool stopping_{};
		};

		SerialQueue& serial_queue(const WorkQueue queue)
		{
			static std::array<SerialQueue, 6> queues;
			return queues[static_cast<size_t>(queue)];
		}

		std::mutex uiWorkMutex;
		std::deque<std::pair<std::uint64_t, std::function<void()>>> uiWork;
		std::uint64_t nextUiWorkToken{1};
		HWND uiWorkWindow{};

		LRESULT CALLBACK ui_work_proc(const HWND window, const UINT message, const WPARAM wparam,
		                              const LPARAM lparam)
		{
			if (message != messageRunUiWork) return DefWindowProcW(window, message, wparam, lparam);
			std::deque<std::pair<std::uint64_t, std::function<void()>>> batch;
			{
				const std::scoped_lock lock(uiWorkMutex);
				batch.swap(uiWork);
			}
			for (auto& [token, work] : batch)
			{
				try { work(); }
				catch (...) {}
			}
			return 0;
		}

		bool start_ui_work_window()
		{
			{
				const std::scoped_lock lock(uiWorkMutex);
				if (uiWorkWindow) return true;
			}
			WNDCLASSEXW description{sizeof(description)};
			description.lpfnWndProc = ui_work_proc;
			description.hInstance = resourceInstance;
			description.lpszClassName = L"ImageWalker30.UiWork";
			if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
			const HWND window = CreateWindowExW(0, description.lpszClassName, nullptr, 0, 0, 0, 0, 0,
			                                    HWND_MESSAGE, nullptr, resourceInstance, nullptr);
			if (!window) return false;
			const std::scoped_lock lock(uiWorkMutex);
			uiWorkWindow = window;
			return true;
		}

		void stop_ui_work_window()
		{
			HWND window{};
			{
				const std::scoped_lock lock(uiWorkMutex);
				window = std::exchange(uiWorkWindow, nullptr);
				uiWork.clear();
			}
			if (window) DestroyWindow(window);
		}
	}

	bool queue_ui(std::function<void()> work)
	{
		if (!work) return false;
		HWND target{};
		std::uint64_t token{};
		{
			const std::scoped_lock lock(uiWorkMutex);
			if (!uiWorkWindow) return false;
			target = uiWorkWindow;
			token = nextUiWorkToken++;
			uiWork.emplace_back(token, std::move(work));
		}
		if (PostMessageW(target, messageRunUiWork, 0, 0)) return true;
		const std::scoped_lock lock(uiWorkMutex);
		const auto found = std::ranges::find(uiWork, token, &decltype(uiWork)::value_type::first);
		if (found != uiWork.end()) uiWork.erase(found);
		return false;
	}

	bool queue_work(const WorkQueue queue, std::function<void()> work)
	{
		return work && serial_queue(queue).push(std::move(work));
	}

	int compare_ordinal_ignore_case(const std::wstring_view left, const std::wstring_view right)
	{
		const int result = CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
		                                        right.data(), static_cast<int>(right.size()), TRUE);
		return result == 0 ? 0 : result - CSTR_EQUAL;
	}

	int compare_file_names(const std::wstring_view left, const std::wstring_view right)
	{
		// StrCmpLogicalW needs null-terminated input; reuse per-thread buffers to keep sorting allocation free.
		thread_local std::wstring leftText;
		thread_local std::wstring rightText;
		leftText.assign(left);
		rightText.assign(right);
		return StrCmpLogicalW(leftText.c_str(), rightText.c_str());
	}

	std::optional<std::filesystem::path> choose_folder(const WindowFramePtr& owner, const std::wstring_view title)
	{
		const HWND nativeOwner = win32::owner_window(owner);
		IFileOpenDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
			return std::nullopt;
		DWORD options{};
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		const std::wstring titleText(title);
		dialog->SetTitle(titleText.c_str());
		std::optional<std::filesystem::path> result;
		if (SUCCEEDED(dialog->Show(nativeOwner)))
		{
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item)))
			{
				result = shell_item_path(item);
				item->Release();
			}
		}
		dialog->Release();
		return result;
	}

	std::optional<std::filesystem::path> choose_open_file(const WindowFramePtr& owner, const OpenFileOptions& options)
	{
		IFileOpenDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
			return std::nullopt;
		const std::wstring filterName(options.filterName);
		const std::wstring filterPattern(options.filterPattern);
		const COMDLG_FILTERSPEC filters[]{
			{filterName.c_str(), filterPattern.c_str()},
			{L"All files", L"*.*"}
		};
		dialog->SetFileTypes(std::size(filters), filters);
		DWORD flags{};
		dialog->GetOptions(&flags);
		dialog->SetOptions(flags | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
		std::optional<std::filesystem::path> result;
		if (SUCCEEDED(dialog->Show(win32::owner_window(owner))))
		{
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item)))
			{
				result = shell_item_path(item);
				item->Release();
			}
		}
		dialog->Release();
		return result;
	}

	std::optional<std::filesystem::path> choose_save_file(const WindowFramePtr& owner, const SaveFileOptions& options)
	{
		const HWND nativeOwner = win32::owner_window(owner);
		IFileSaveDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
			return std::nullopt;
		const std::wstring filterName(options.filterName);
		const std::wstring filterPattern(options.filterPattern);
		const std::wstring defaultExtension(options.defaultExtension);
		const std::wstring initialName(options.initialName);
		const COMDLG_FILTERSPEC filter{filterName.c_str(), filterPattern.c_str()};
		dialog->SetFileTypes(1, &filter);
		dialog->SetDefaultExtension(defaultExtension.c_str());
		dialog->SetFileName(initialName.c_str());
		std::optional<std::filesystem::path> result;
		if (SUCCEEDED(dialog->Show(nativeOwner)))
		{
			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->GetResult(&item)))
			{
				result = shell_item_path(item);
				item->Release();
			}
		}
		dialog->Release();
		return result;
	}

	int read_integer_setting(const std::wstring_view section, const std::wstring_view key, const int fallback)
	{
		const std::wstring sectionText(section);
		const std::wstring keyText(key);
		return static_cast<int>(GetPrivateProfileIntW(
			sectionText.c_str(), keyText.c_str(), fallback, settings_path().c_str()));
	}

	void write_integer_settings(const std::wstring_view section, const std::span<const IntegerSetting> values)
	{
		const auto path = settings_path();
		const std::wstring sectionText(section);
		for (const auto& value : values)
		{
			const auto text = std::to_wstring(value.value);
			WritePrivateProfileStringW(sectionText.c_str(), value.key.c_str(), text.c_str(), path.c_str());
		}
		WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
	}

	std::wstring read_text_setting(const std::wstring_view section, const std::wstring_view key,
	                               const std::wstring_view fallback)
	{
		const std::wstring sectionText(section);
		const std::wstring keyText(key);
		const std::wstring fallbackText(fallback);
		std::wstring buffer(1024, L'\0');
		const DWORD written = GetPrivateProfileStringW(sectionText.c_str(), keyText.c_str(), fallbackText.c_str(),
		                                               buffer.data(), static_cast<DWORD>(buffer.size()),
		                                               settings_path().c_str());
		buffer.resize(written);
		return buffer;
	}

	void write_text_settings(const std::wstring_view section, const std::span<const TextSetting> values)
	{
		const auto path = settings_path();
		const std::wstring sectionText(section);
		for (const auto& value : values)
			WritePrivateProfileStringW(sectionText.c_str(), value.key.c_str(),
			                           value.value.empty() ? nullptr : value.value.c_str(), path.c_str());
		WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
	}

	void clear_settings_section(const std::wstring_view section)
	{
		const std::wstring sectionText(section);
		WritePrivateProfileStringW(sectionText.c_str(), nullptr, nullptr, settings_path().c_str());
	}

	std::filesystem::path module_folder()
	{
		return settings_path().parent_path();
	}

	std::filesystem::path known_folder(const KnownFolder folder)
	{
		const KNOWNFOLDERID* identifier = &FOLDERID_Pictures;
		switch (folder)
		{
		case KnownFolder::documents: identifier = &FOLDERID_Documents; break;
		case KnownFolder::desktop: identifier = &FOLDERID_Desktop; break;
		case KnownFolder::downloads: identifier = &FOLDERID_Downloads; break;
		case KnownFolder::music: identifier = &FOLDERID_Music; break;
		case KnownFolder::videos: identifier = &FOLDERID_Videos; break;
		default: break;
		}
		PWSTR text = nullptr;
		std::filesystem::path result;
		if (SUCCEEDED(SHGetKnownFolderPath(*identifier, KF_FLAG_DEFAULT, nullptr, &text)) && text) result = text;
		if (text) CoTaskMemFree(text);
		std::error_code error;
		if (!result.empty() && !std::filesystem::is_directory(result, error)) result.clear();
		return result;
	}

	std::vector<std::filesystem::path> win32::dropped_files(const HANDLE drop)
	{
		std::vector<std::filesystem::path> result;
		const auto nativeDrop = static_cast<HDROP>(drop);
		if (!nativeDrop) return result;
		const UINT count = DragQueryFileW(nativeDrop, 0xffffffff, nullptr, 0);
		result.reserve(count);
		std::wstring path;
		for (UINT index = 0; index < count; ++index)
		{
			const UINT length = DragQueryFileW(nativeDrop, index, nullptr, 0);
			path.assign(static_cast<size_t>(length) + 1, L'\0');
			if (!DragQueryFileW(nativeDrop, index, path.data(), length + 1)) continue;
			path.resize(length);
			result.emplace_back(path);
		}
		return result;
	}

	ClipboardStatus clipboard_status()
	{
		return {
			IsClipboardFormatAvailable(CF_HDROP) != FALSE,
			IsClipboardFormatAvailable(CF_DIBV5) != FALSE ||
			IsClipboardFormatAvailable(CF_DIB) != FALSE ||
			IsClipboardFormatAvailable(CF_BITMAP) != FALSE
		};
	}

	bool set_clipboard_files(const WindowFramePtr& owner, const std::span<const std::filesystem::path> paths,
	                         const bool move)
	{
		if (paths.empty()) return false;
		size_t characters = 1;
		for (const auto& path : paths) characters += path.wstring().size() + 1;
		const HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
		                                 sizeof(DROPFILES) + characters * sizeof(wchar_t));
		if (!data) return false;
		auto* drop = static_cast<DROPFILES*>(GlobalLock(data));
		if (!drop)
		{
			GlobalFree(data);
			return false;
		}
		drop->pFiles = sizeof(DROPFILES);
		drop->fWide = TRUE;
		auto* output = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
		for (const auto& path : paths)
		{
			const auto text = path.wstring();
			std::copy(text.begin(), text.end(), output);
			output += text.size();
			*output++ = L'\0';
		}
		*output = L'\0';
		GlobalUnlock(data);
		if (!OpenClipboard(win32::owner_window(owner)))
		{
			GlobalFree(data);
			return false;
		}
		EmptyClipboard();
		const bool filesSet = SetClipboardData(CF_HDROP, data) != nullptr;
		if (!filesSet) GlobalFree(data);
		const UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
		const HGLOBAL effectData = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
		if (effectData)
		{
			auto* effect = static_cast<DWORD*>(GlobalLock(effectData));
			if (effect)
			{
				*effect = move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
				GlobalUnlock(effectData);
				if (!SetClipboardData(format, effectData)) GlobalFree(effectData);
			}
			else GlobalFree(effectData);
		}
		CloseClipboard();
		return filesSet;
	}

	ClipboardContent read_clipboard(const WindowFramePtr& owner)
	{
		ClipboardContent result;
		if (!OpenClipboard(win32::owner_window(owner))) return result;
		result.files = win32::dropped_files(GetClipboardData(CF_HDROP));
		if (result.files.empty())
		{
			for (const UINT format : {CF_DIBV5, CF_DIB})
			{
				const HBITMAP bitmap = bitmap_from_dib(GetClipboardData(format));
				if (!bitmap) continue;
				result.image = bitmap_resource(bitmap);
				DeleteObject(bitmap);
				if (!result.image.pixels.empty()) break;
			}
			if (result.image.pixels.empty())
				result.image = bitmap_resource(static_cast<HBITMAP>(GetClipboardData(CF_BITMAP)));
		}
		CloseClipboard();
		return result;
	}

	bool perform_file_operation(const WindowFramePtr& owner, const FileOperation operation,
	                            const std::span<const std::filesystem::path> sources,
	                            const std::filesystem::path& destination)
	{
		const std::wstring sourceList = path_list(sources);
		if (sourceList.empty() || (operation != FileOperation::recycle && destination.empty())) return false;
		std::wstring destinationList;
		if (!destination.empty())
		{
			destinationList = destination.wstring();
			destinationList.push_back(L'\0');
			destinationList.push_back(L'\0');
		}
		SHFILEOPSTRUCTW nativeOperation{};
		nativeOperation.hwnd = win32::owner_window(owner);
		nativeOperation.wFunc = operation == FileOperation::copy
			                        ? FO_COPY
			                        : operation == FileOperation::move
			                        ? FO_MOVE
			                        : FO_DELETE;
		nativeOperation.pFrom = sourceList.c_str();
		nativeOperation.pTo = destinationList.empty() ? nullptr : destinationList.c_str();
		nativeOperation.fFlags = FOF_ALLOWUNDO |
			(operation == FileOperation::recycle ? 0 : FOF_NOCONFIRMMKDIR);
		return SHFileOperationW(&nativeOperation) == 0 && !nativeOperation.fAnyOperationsAborted;
	}

	bool delete_permanently(const std::span<const std::filesystem::path> paths, std::error_code& error)
	{
		error.clear();
		bool allRemoved = true;
		for (const auto& path : paths)
		{
			// A remote folder is often a UNC path, which needs the \\?\UNC\ form, not a bare prefix.
			const auto native = files::native_path(path);
			std::error_code removeError;
			if (std::filesystem::remove(native, removeError) && !removeError) continue;
			// A read-only file refuses to go; clearing the attribute is the whole difference.
			if (SetFileAttributesW(native.c_str(), FILE_ATTRIBUTE_NORMAL))
			{
				removeError.clear();
				if (std::filesystem::remove(native, removeError) && !removeError) continue;
			}
			allRemoved = false;
			if (!error) error = removeError ? removeError : std::make_error_code(std::errc::io_error);
		}
		return allRemoved;
	}

	namespace
	{
		class ShellAppHandler final : public AppHandler
		{
		public:
			ShellAppHandler(IAssocHandler* handler, std::wstring name)
				: handler_(handler), name_(std::move(name))
			{
			}

			~ShellAppHandler() override { if (handler_) handler_->Release(); }
			ShellAppHandler(const ShellAppHandler&) = delete;
			ShellAppHandler& operator=(const ShellAppHandler&) = delete;

			const std::wstring& name() const override { return name_; }

			bool invoke(const std::span<const std::filesystem::path> paths) const override
			{
				if (!handler_ || paths.empty()) return false;
				std::vector<PIDLIST_ABSOLUTE> items;
				for (const auto& path : paths)
				{
					const auto pidl = ILCreateFromPathW(path.c_str());
					if (!pidl)
					{
						for (const auto item : items) ILFree(item);
						return false;
					}
					items.push_back(pidl);
				}
				bool invoked = false;
				if (!items.empty())
				{
					IShellItemArray* array = nullptr;
					if (SUCCEEDED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(items.size()),
						const_cast<LPCITEMIDLIST*>(items.data()), &array)) && array)
					{
						IDataObject* data = nullptr;
						if (SUCCEEDED(array->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))) && data)
						{
							invoked = SUCCEEDED(handler_->Invoke(data));
							data->Release();
						}
						array->Release();
					}
				}
				for (const auto pidl : items) ILFree(pidl);
				return invoked;
			}

		private:
			IAssocHandler* handler_{};
			std::wstring name_;
		};
	}

	std::vector<AppHandlerPtr> registered_apps_for_extension(const std::wstring_view extension)
	{
		std::vector<AppHandlerPtr> result;
		if (extension.empty() || extension.front() != L'.') return result;
		const std::wstring key(extension);
		IEnumAssocHandlers* handlers = nullptr;
		if (FAILED(SHAssocEnumHandlers(key.c_str(), ASSOC_FILTER_RECOMMENDED, &handlers)) || !handlers)
			return result;
		constexpr size_t maximumHandlers = 32;
		for (;;)
		{
			IAssocHandler* handler = nullptr;
			ULONG fetched = 0;
			if (handlers->Next(1, &handler, &fetched) != S_OK || !fetched || !handler) break;
			LPWSTR uiName = nullptr;
			std::wstring name;
			if (SUCCEEDED(handler->GetUIName(&uiName)) && uiName)
			{
				name = uiName;
				CoTaskMemFree(uiName);
			}
			if (name.empty()) handler->Release();
			else result.push_back(std::make_shared<ShellAppHandler>(handler, std::move(name)));
			if (result.size() >= maximumHandlers) break;
		}
		handlers->Release();
		return result;
	}

	bool launch_process(const std::filesystem::path& executable, const std::wstring_view arguments,
	                    const WindowFramePtr& owner)
	{
		if (executable.empty()) return false;
		const std::wstring argumentText(arguments);
		SHELLEXECUTEINFOW information{sizeof(information)};
		information.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		information.hwnd = win32::owner_window(owner);
		information.lpVerb = L"open";
		information.lpFile = executable.c_str();
		information.lpParameters = argumentText.empty() ? nullptr : argumentText.c_str();
		information.lpDirectory = nullptr;
		information.nShow = SW_SHOWNORMAL;
		return ShellExecuteExW(&information) != FALSE;
	}

	namespace
	{
		class WinTextInput final : public TextInput
		{
		public:
			WinTextInput(const HWND parent, TextInputOptions options)
				: options_(std::move(options)), parent_(parent)
			{
				if (!parent_) return;
				static UINT_PTR nextSubclassId = 1;
				subclassId_ = nextSubclassId++;
				control_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", options_.text.c_str(),
				                           WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
				                           parent_, reinterpret_cast<HMENU>(subclassId_),
				                           GetModuleHandleW(nullptr), nullptr);
				if (!control_) return;
				if (options_.folderCompletion)
					SHAutoComplete(control_, SHACF_FILESYS_DIRS | SHACF_AUTOSUGGEST_FORCE_ON);
				if (!options_.cueBanner.empty())
					SendMessageW(control_, EM_SETCUEBANNER, TRUE,
					             reinterpret_cast<LPARAM>(options_.cueBanner.c_str()));
				SetWindowSubclass(parent_, parent_proc, subclassId_, reinterpret_cast<DWORD_PTR>(this));
				SetWindowSubclass(control_, control_proc, 1, reinterpret_cast<DWORD_PTR>(this));
			}

			~WinTextInput() override
			{
				if (parent_ && IsWindow(parent_)) RemoveWindowSubclass(parent_, parent_proc, subclassId_);
				if (control_ && IsWindow(control_))
				{
					RemoveWindowSubclass(control_, control_proc, 1);
					DestroyWindow(control_);
				}
			}

			WinTextInput(const WinTextInput&) = delete;
			WinTextInput& operator=(const WinTextInput&) = delete;

			void set_bounds(const recti bounds) override
			{
				if (control_) MoveWindow(control_, bounds.x, bounds.y, bounds.width, bounds.height, TRUE);
			}

			void show(const bool visible) override
			{
				if (control_) ShowWindow(control_, visible ? SW_SHOW : SW_HIDE);
			}

			void set_focus() override { if (control_) SetFocus(control_); }
			bool has_focus() const override { return control_ && GetFocus() == control_; }

			std::wstring text() const override
			{
				if (!control_) return {};
				const int length = GetWindowTextLengthW(control_);
				if (length <= 0) return {};
				std::wstring result(static_cast<size_t>(length), L'\0');
				const int written = GetWindowTextW(control_, result.data(), length + 1);
				result.resize(static_cast<size_t>((std::max)(0, written)));
				return result;
			}

			void set_text(const std::wstring_view value) override
			{
				if (!control_ || text() == value) return;
				suppress_ = true;
				SetWindowTextW(control_, std::wstring(value).c_str());
				suppress_ = false;
			}

			void set_font(const FontPtr& font) override
			{
				if (!control_ || !font || !*font) return;
				SendMessageW(control_, WM_SETFONT,
				             reinterpret_cast<WPARAM>(reinterpret_cast<HFONT>(font->native_handle())), TRUE);
			}

		private:
			static LRESULT CALLBACK parent_proc(const HWND window, const UINT message, const WPARAM wparam,
			                                    const LPARAM lparam, UINT_PTR, const DWORD_PTR reference)
			{
				auto* self = reinterpret_cast<WinTextInput*>(reference);
				if (self && message == WM_COMMAND && reinterpret_cast<HWND>(lparam) == self->control_ &&
					HIWORD(wparam) == EN_CHANGE && !self->suppress_ && self->options_.changed)
					self->options_.changed(self->text());
				return DefSubclassProc(window, message, wparam, lparam);
			}

			static LRESULT CALLBACK control_proc(const HWND window, const UINT message, const WPARAM wparam,
			                                     const LPARAM lparam, UINT_PTR, const DWORD_PTR reference)
			{
				auto* self = reinterpret_cast<WinTextInput*>(reference);
				if (self && wparam == VK_RETURN && (message == WM_KEYDOWN || message == WM_CHAR))
				{
					// A single-line edit beeps on Enter unless the key is consumed here.
					if (message == WM_KEYDOWN)
						if (const auto accepted = self->options_.accepted) accepted();
					return 0;
				}
				if (self && message == WM_KEYDOWN && wparam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
				{
					SendMessageW(window, EM_SETSEL, 0, -1);
					return 0;
				}
				if (self && (message == WM_KEYDOWN || message == WM_CHAR) &&
					(wparam == VK_ESCAPE || wparam == VK_TAB))
				{
					if (message == WM_KEYDOWN)
					{
						if (wparam == VK_ESCAPE)
						{
							if (const auto escaped = self->options_.escaped) escaped();
						}
						else if (const auto tabbed = self->options_.tabbed)
							tabbed((GetKeyState(VK_SHIFT) & 0x8000) != 0);
					}
					return 0;
				}
				return DefSubclassProc(window, message, wparam, lparam);
			}

			TextInputOptions options_;
			HWND parent_{};
			HWND control_{};
			UINT_PTR subclassId_{};
			bool suppress_{};
		};
	}

	TextInputPtr create_text_input(const WindowFramePtr& owner, TextInputOptions options)
	{
		const HWND parent = win32::owner_window(owner);
		if (!parent) return {};
		return std::make_shared<WinTextInput>(parent, std::move(options));
	}

	Runtime::Runtime(const RuntimeMode mode) : mode_(mode)
	{
		if (mode == RuntimeMode::multithreaded)
		{
			initialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
			return;
		}

		if (FAILED(OleInitialize(nullptr))) return;
		INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
		initialized_ = InitCommonControlsEx(&controls) != FALSE && start_ui_work_window();
		if (!initialized_) OleUninitialize();
	}

	Runtime::~Runtime()
	{
		if (!initialized_) return;
		if (mode_ == RuntimeMode::multithreaded) CoUninitialize();
		else
		{
			stop_ui_work_window();
			OleUninitialize();
		}
	}

	void show_error(const std::wstring_view message, const std::wstring_view title, const WindowFramePtr& owner)
	{
		const std::wstring messageText(message);
		const std::wstring titleText(title);
		MessageBoxW(win32::owner_window(owner), messageText.c_str(), titleText.c_str(), MB_OK | MB_ICONERROR);
	}

	void write_diagnostic(const std::wstring_view text)
	{
		if (diagnostics::run_log().write(text))
			OutputDebugStringW(L"ImageWalker: diagnostic file write failed; debugger/console output follows.\n");
		// A windowed process owns no console, so a headless run borrows the one that launched it.
		[[maybe_unused]] static const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
		const std::wstring value(text);
		OutputDebugStringW(value.c_str());
		const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
		if (!console || console == INVALID_HANDLE_VALUE) return;
		DWORD written = 0;
		if (WriteConsoleW(console, value.c_str(), static_cast<DWORD>(value.size()), &written, nullptr)) return;
		// WriteConsoleW only accepts a console handle, so a redirected run needs bytes instead.
		const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
		                                       nullptr, 0, nullptr, nullptr);
		if (length <= 0) return;
		std::string bytes(static_cast<size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), bytes.data(), length,
		                    nullptr, nullptr);
		WriteFile(console, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
	}

	std::filesystem::path unique_temp_folder(const std::wstring_view name)
	{
		std::error_code error;
		auto folder = std::filesystem::temp_directory_path(error);
		if (error) return {};
		folder /= std::format(L"{}-{}", name, GetCurrentProcessId());
		return folder;
	}

	FileAttributes read_file_attributes(const std::filesystem::path& path)
	{
		FileAttributes result;
		const DWORD attributes = GetFileAttributesW(path.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES) return result;
		result.known = true;
		result.hidden = (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
		result.directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		result.reparse = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
		return result;
	}

	bool has_sse2()
	{
#if defined(_M_X64) || defined(_M_ARM64)
		return true;
#else
		return IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) != FALSE;
#endif
	}

	bool has_avx2()
	{
#if defined(_M_X64) || defined(_M_IX86)
		static const bool supported = []
		{
			int registers[4]{};
			__cpuid(registers, 1);
			constexpr int osxsave = 1 << 27;
			constexpr int avx = 1 << 28;
			if ((registers[2] & (osxsave | avx)) != (osxsave | avx) || (_xgetbv(0) & 0x6) != 0x6)
				return false;
			__cpuidex(registers, 7, 0);
			return (registers[1] & (1 << 5)) != 0;
		}();
		return supported;
#else
		return false;
#endif
	}

	void open_uri(const std::wstring_view uri)
	{
		const std::wstring value(uri);
		ShellExecuteW(nullptr, L"open", value.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	bool win32::wait_for_message()
	{
		return MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE) ==
			WAIT_OBJECT_0;
	}

	int run_ui_loop(const WindowFramePtr& window)
	{
		if (!start_ui_work_window()) return 1;
		const auto nativeFrame = std::dynamic_pointer_cast<WinWindowFrame>(window);
		MSG message{};
		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			if (!nativeFrame || !nativeFrame->translate_accelerator(message))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		// Joining the queues before the caller's state unwinds is what lets work items capture it directly.
		for (const auto queue : {
			     WorkQueue::image, WorkQueue::thumbnail, WorkQueue::metadata, WorkQueue::folder, WorkQueue::shell,
			     WorkQueue::task
		     })
			serial_queue(queue).stop();
		stop_ui_work_window();
		return static_cast<int>(message.wParam);
	}
}

int WINAPI wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, const int showCommand)
{
	iw::platform::resourceInstance = instance;
	int argumentCount = 0;
	const auto nativeArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	std::vector<std::wstring_view> arguments;
	if (nativeArguments)
	{
		arguments.reserve(argumentCount > 1 ? static_cast<size_t>(argumentCount - 1) : 0);
		for (int index = 1; index < argumentCount; ++index) arguments.emplace_back(nativeArguments[index]);
	}
	const int result = imagewalker_main(showCommand, arguments);
	if (nativeArguments) LocalFree(nativeArguments);
	return result;
}
