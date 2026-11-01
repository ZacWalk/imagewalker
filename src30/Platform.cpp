// ImageWalker by Zac Walker
// Implements Windows runtime, shell, and natural-ordering services.

#include "Platform.h"
#include "DesktopServices.h"
#include "Files.h"
#include "PixelOps.h"
#include "Resource.h"
#include "PlatformWin32.h"
#include "DiagnosticLog.h"
#include "platform_win_interop.h"
#include "ui/ui.h"
#include "ui/test_support.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace iw::platform
{
	void show_popup_menu_pf(const WindowFramePtr& owner, app::pointi screenPoint,
	                        std::span<const PopupItem> items);

	HWND win32::owner_window(const WindowFramePtr& frame)
	{
		return frame ? reinterpret_cast<HWND>(frame->native_handle()) : nullptr;
	}

	struct Font::Impl
	{
		pf::font_handle handle{};
		~Impl() { if (handle) pf::delete_font_handle(handle); }
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
		return impl_ ? static_cast<NativeHandle>(impl_->handle) : 0;
	}

	FontPtr create_message_font(const unsigned int dpi)
	{
		auto impl = std::make_unique<Font::Impl>();
		impl->handle = pf::create_font_handle({"Segoe UI", static_cast<int>(9.0 * (dpi ? dpi : 96) / 96),
		                                      400});
		if (!impl->handle) return {};
		return std::shared_ptr<Font>(new Font(std::move(impl)));
	}

	color system_color(const SystemColor value)
	{
		switch (value)
		{
		case SystemColor::window: return {255, 255, 255};
		case SystemColor::windowText: return {32, 32, 32};
		case SystemColor::face: return {245, 245, 245};
		case SystemColor::shadow: return {128, 128, 128};
		case SystemColor::highlight: return {51, 103, 180};
		case SystemColor::highlightText: return {255, 255, 255};
		case SystemColor::grayText: return {128, 128, 128};
		case SystemColor::buttonText: return {32, 32, 32};
		case SystemColor::scrollbar: return {220, 220, 220};
		}
		return colors::black;
	}

	color calc_hande_color(const bool hover, const bool selected, const bool checked)
	{
		if (selected) return system_color(SystemColor::highlight);
		if (hover) return color{188, 218, 246};
		if (checked) return color{214, 228, 242};
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
		PopupItem result;
		result.separator = true;
		return result;
	}

	void show_popup_menu(const WindowFramePtr& owner, const app::pointi screenPoint,
	                     const std::span<const PopupItem> items)
	{
		show_popup_menu_pf(owner, screenPoint, items);
	}

	std::wstring path_ellipsis(const std::wstring_view path, const int width,
	                           const std::function<int(std::wstring_view)>& measure)
	{
		if (!measure || measure(path) <= width) return std::wstring(path);
		constexpr std::wstring_view ellipsis = L"...";
		// The longest prefix that fits, never ending between the halves of a surrogate pair.
		const auto fitting = [&measure](const std::wstring_view text, const int available)
		{
			size_t low = 0;
			for (size_t high = text.size(); low < high;)
			{
				const size_t middle = high - (high - low) / 2;
				if (measure(text.substr(0, middle)) <= available) low = middle;
				else high = middle - 1;
			}
			if (low > 0 && low < text.size() && text[low - 1] >= 0xd800 && text[low - 1] <= 0xdbff &&
				text[low] >= 0xdc00 && text[low] <= 0xdfff)
				--low;
			return text.substr(0, low);
		};
		std::wstring_view name = path;
		const size_t end = path.find_last_not_of(L"\\/");
		const size_t separator = end == std::wstring_view::npos ? end : path.find_last_of(L"\\/", end);
		if (separator != std::wstring_view::npos && separator > 0)
		{
			std::wstring kept(ellipsis);
			kept.append(path.substr(separator));
			if (const int keptWidth = measure(kept); keptWidth <= width)
				return std::wstring(fitting(path.substr(0, separator), width - keptWidth)) + kept;
			name = path.substr(separator + 1);
			if (measure(name) <= width) return std::wstring(name);
		}
		return std::wstring(fitting(name, width - measure(ellipsis))) + std::wstring(ellipsis);
	}

	// The application keeps its old event vocabulary and task-owned chrome, but the
	// actual frame, dispatcher and drawing surface belong to libdesktop.  This
	// adapter contains no window procedure or message loop; it only translates the
	// values used by the ImageWalker model.
	class LibdesktopWindowFrame;

	class LibdesktopDrawContext final : public DrawContext
	{
	public:
		explicit LibdesktopDrawContext(pf::draw_context& draw) : draw_(draw) {}

		app::recti clip_rect() const override
		{
			const auto clip = draw_.clip_rect();
			return {clip.left, clip.top, clip.width(), clip.height()};
		}

		void fill(const app::recti bounds, const color value) override
		{
			draw_.fill_solid_rect({bounds.x, bounds.y, bounds.right(), bounds.bottom()},
			                      {value.r, value.g, value.b});
		}

		void blend_fill(const app::recti bounds, const color value) override
		{
			draw_.fill_blend_rect({bounds.x, bounds.y, bounds.right(), bounds.bottom()},
			                      {value.r, value.g, value.b});
		}

		void text(const std::wstring_view value, const app::recti bounds, const color textColor,
		          const std::uint32_t format, const FontPtr& font) override
		{
			if (value.empty() || bounds.width <= 0 || bounds.height <= 0) return;
			std::wstring shortened;
			std::wstring_view displayed = value;
			const auto measure = [this, &font](const std::wstring_view part)
			{
				return measure_text(part, TextFormat::singleLine, font).width;
			};
			auto extent = measure_text(displayed, TextFormat::singleLine, font);
			if ((format & TextFormat::pathEllipsis) && extent.width > bounds.width)
			{
				shortened = path_ellipsis(value, bounds.width, measure);
				displayed = shortened;
				extent = measure_text(displayed, TextFormat::singleLine, font);
			}
			else if ((format & TextFormat::endEllipsis) && extent.width > bounds.width)
			{
				constexpr std::wstring_view ellipsis = L"...";
				const auto available = (std::max)(0, bounds.width - measure(ellipsis));
				size_t low = 0;
				size_t high = value.size();
				while (low < high)
				{
					const size_t middle = low + (high - low + 1) / 2;
					if (measure(value.substr(0, middle)) <= available) low = middle;
					else high = middle - 1;
				}
				shortened.assign(value.substr(0, low));
				shortened += ellipsis;
				displayed = shortened;
				extent = measure_text(displayed, TextFormat::singleLine, font);
			}
			int x = bounds.x;
			if (format & TextFormat::center) x += (bounds.width - extent.width) / 2;
			else if (format & TextFormat::right) x += bounds.width - extent.width;
			int y = bounds.y;
			if (format & TextFormat::verticalCenter) y += (bounds.height - extent.height) / 2;
			const auto utf8 = pf::utf16_to_utf8(displayed);
			const auto handle = font && *font ? static_cast<pf::font_handle>(font->native_handle()) : 0;
			if (handle)
				draw_.draw_text_h(x, y, utf8, handle, {textColor.r, textColor.g, textColor.b});
			else
				draw_.draw_text(x, y, {x, y, x + extent.width, y + extent.height}, utf8,
				                {9, pf::font_name::arial}, {textColor.r, textColor.g, textColor.b},
				                {255, 255, 255});
		}

		app::sizei measure_text(const std::wstring_view value, const std::uint32_t,
		                   const FontPtr& font) override
		{
			const auto utf8 = pf::utf16_to_utf8(value);
			const auto handle = font && *font ? static_cast<pf::font_handle>(font->native_handle()) : 0;
			const auto extent = handle
				                    ? draw_.measure_text_h(utf8, handle)
				                    : draw_.measure_text(utf8, {9, pf::font_name::arial});
			return {extent.cx, extent.cy};
		}

		void image(const std::span<const std::uint32_t> pixels, const app::sizei source,
		           const app::recti destination, const bool) override
		{
			if (source.width <= 0 || source.height <= 0 ||
				pixels.size() < static_cast<size_t>(source.width) * source.height)
				return;
			std::vector<std::uint32_t> owned(pixels.begin(),
			                                 pixels.begin() + static_cast<size_t>(source.width) * source.height);
			pf::bitmap bitmap(source.width, source.height, std::move(owned));
			draw_.draw_bitmap({destination.x, destination.y, destination.right(), destination.bottom()}, bitmap, true);
		}

		void focus(const app::recti bounds) override { outline(bounds, {0, 0, 0}, 1); }

		void outline(const app::recti bounds, const color value, const int width) override
		{
			const auto c = pf::color_t{value.r, value.g, value.b};
			const auto left = pf::ipoint(bounds.x, bounds.y);
			const auto right = pf::ipoint(bounds.right(), bounds.bottom());
			draw_.draw_solid_line(left, {right.x, left.y}, c, width);
			draw_.draw_solid_line({right.x, left.y}, right, c, width);
			draw_.draw_solid_line(right, {left.x, right.y}, c, width);
			draw_.draw_solid_line({left.x, right.y}, left, c, width);
		}

		void clip(const app::recti bounds) override
		{
			draw_.set_clip_rect({bounds.x, bounds.y, bounds.right(), bounds.bottom()});
		}

		void reset_clip() override { draw_.clear_clip_rect(); }

	private:
		pf::draw_context& draw_;
	};

	class LibdesktopMeasureContext final : public MeasureContext
	{
	public:
		explicit LibdesktopMeasureContext(pf::measure_context& measure) : measure_(measure) {}

		app::sizei measure_text(const std::wstring_view value, const std::uint32_t,
		                   const FontPtr& font) const override
		{
			const auto utf8 = pf::utf16_to_utf8(value);
			const auto handle = font && *font ? static_cast<pf::font_handle>(font->native_handle()) : 0;
			const auto extent = handle
				                    ? pf::measure_text_with_font(handle, utf8)
				                    : measure_.measure_text(utf8, {9, pf::font_name::arial});
			return {extent.cx, extent.cy};
		}

	private:
		pf::measure_context& measure_;
	};

	class LibdesktopReactor final : public pf::frame_reactor
	{
	public:
		explicit LibdesktopReactor(const std::shared_ptr<LibdesktopWindowFrame>& owner) : owner_(owner) {}

		std::uint32_t handle_message(pf::window_frame_ptr, const pf::message_type message,
		                             const pf::message_params& params) override;
		std::uint32_t handle_mouse(pf::window_frame_ptr, const pf::mouse_message_type message,
		                           const pf::mouse_params& params) override;
		std::uint32_t handle_keyboard(pf::window_frame_ptr, const pf::keyboard_message_type message,
		                              const pf::keyboard_params& params) override;
		std::uint32_t handle_gesture(pf::window_frame_ptr, const pf::gesture_params& params) override;
		void handle_paint(pf::window_frame_ptr&, pf::draw_context& draw) override;
		void handle_size(pf::window_frame_ptr&, pf::isize extent, pf::measure_context& measure) override;

	private:
		std::weak_ptr<LibdesktopWindowFrame> owner_;
	};

	class LibdesktopWindowFrame final : public WindowFrame,
		public std::enable_shared_from_this<LibdesktopWindowFrame>
	{
	public:
		explicit LibdesktopWindowFrame(pf::window_frame_ptr native) : native_(std::move(native)) {}

		NativeHandle native_handle() const override
		{
			return native_ ? pf::platform_win::native_window_handle(native_) : 0;
		}

		void set_reactor(FrameReactorPtr reactor) override
		{
			reactor_ = std::move(reactor);
			if (native_)
			{
				native_->set_reactor(reactor_
					                     ? std::make_shared<LibdesktopReactor>(shared_from_this())
					                     : pf::frame_reactor_ptr{});
			}
		}

		WindowFramePtr create_child(FrameReactorPtr reactor, const WindowOptions& options) override
		{
			if (!native_) return {};
			std::uint32_t style = pf::window_style::child;
			if (options.visible) style |= pf::window_style::visible;
			if (options.clipChildren) style |= pf::window_style::clip_children;
			const auto className = options.className.empty() ? "ImageWalker30.Child" : options.className;
			auto child = native_->create_child(className, style, {});
			if (!child) return {};
			auto result = std::make_shared<LibdesktopWindowFrame>(std::move(child));
			result->set_reactor(std::move(reactor));
			if (options.bounds.width > 0 && options.bounds.height > 0)
				result->move(options.bounds);
			result->show(options.visible);
			return result;
		}

		app::recti client_rect() const override
		{
			if (!native_) return {};
			const auto value = native_->get_client_rect();
			return {value.left, value.top, value.width(), value.height()};
		}

		void move(const app::recti bounds) override
		{
			pendingBounds_ = bounds;
			if (native_handle()) native_->move_window({bounds.x, bounds.y, bounds.right(), bounds.bottom()});
		}

		void show(const bool visible) override
		{
			pendingVisible_ = visible;
			if (native_handle()) native_->show(visible);
		}

		void invalidate() override { if (native_) native_->invalidate(); }

		void invalidate(const app::recti bounds) override
		{
			if (native_) native_->invalidate_rect({bounds.x, bounds.y, bounds.right(), bounds.bottom()});
		}

		void set_focus() override { if (native_) native_->set_focus(); }
		bool has_focus() const override { return native_ && native_->has_focus(); }
		void set_capture() override { if (native_) native_->set_capture(); }
		void release_capture() override { if (native_) native_->release_capture(); }
		void track_mouse_leave() override { if (native_) native_->track_mouse_leave(); }
		void configure_gestures(const bool zoom, const bool pan) override
		{
			if (native_) native_->configure_gestures(zoom, pan);
		}

		void start_timer(const unsigned int milliseconds) override
		{
			if (native_) native_->set_timer(1, (std::max)(1u, milliseconds));
		}

		void stop_timer() override { if (native_) native_->kill_timer(1); }

		void accept_file_drops(const bool accept) override
		{
			if (native_) native_->accept_drop_files(accept);
		}

		void set_cursor(const CursorShape cursor) override
		{
			if (!native_) return;
			native_->set_cursor_shape(cursor == CursorShape::sizeHorizontal ? pf::cursor_shape::size_we :
				cursor == CursorShape::sizeAll ? pf::cursor_shape::hand :
				cursor == CursorShape::wait ? pf::cursor_shape::wait :
				cursor == CursorShape::zoom ? pf::cursor_shape::hand : pf::cursor_shape::arrow);
		}

		app::pointi screen_to_client(const app::pointi point) const override
		{
			if (!native_) return point;
			const auto result = native_->screen_to_client({point.x, point.y});
			return {result.x, result.y};
		}

		app::pointi client_to_screen(const app::pointi point) const override
		{
			if (!native_) return point;
			const auto result = native_->client_to_screen({point.x, point.y});
			return {result.x, result.y};
		}

		void set_title(const std::wstring_view title) override
		{
			pendingTitle_ = std::wstring(title);
			if (native_handle()) native_->set_text(pf::utf16_to_utf8(title));
		}

		void set_fullscreen(const bool fullscreen) override
		{
			pendingFullscreen_ = fullscreen;
			if (native_handle()) pf::platform_win::set_fullscreen(native_, fullscreen);
		}

		void set_maximized(const bool maximized) override
		{
			pendingMaximized_ = maximized;
			if (native_handle())
			{
				auto placement = native_->get_placement();
				placement.maximized = maximized;
				native_->set_placement(placement);
			}
		}

		WindowPlacement placement() const override
		{
			if (!native_) return {pendingBounds_, pendingMaximized_};
			const auto value = native_->get_placement();
			return {{value.normal_bounds.left, value.normal_bounds.top,
			         value.normal_bounds.width(), value.normal_bounds.height()}, value.maximized};
		}

		void set_accelerators(const std::span<const CommandAccelerator>) override {}
		void close() override { if (native_) native_->close(); }
		unsigned int dpi() const override
		{
			return native_ ? static_cast<unsigned int>(native_->get_dpi_scale() * 96.0 + 0.5) : 96;
		}

		const pf::window_frame_ptr& native_frame() const { return native_; }
		const FrameReactorPtr& reactor() const { return reactor_; }

		void apply_pending()
		{
			if (!native_) return;
			if (!pendingTitle_.empty()) native_->set_text(pf::utf16_to_utf8(pendingTitle_));
			if (pendingBounds_.width > 0 && pendingBounds_.height > 0)
				native_->move_window({pendingBounds_.x, pendingBounds_.y,
				                      pendingBounds_.right(), pendingBounds_.bottom()});
			if (pendingMaximized_)
			{
				auto value = native_->get_placement();
				value.maximized = true;
				native_->set_placement(value);
			}
			if (pendingFullscreen_) pf::platform_win::set_fullscreen(native_, true);
		}

	private:
		pf::window_frame_ptr native_;
		FrameReactorPtr reactor_;
		app::recti pendingBounds_{};
		std::wstring pendingTitle_;
		bool pendingVisible_{true};
		bool pendingFullscreen_{};
		bool pendingMaximized_{};
	};

	std::uint32_t LibdesktopReactor::handle_message(pf::window_frame_ptr,
	                                                const pf::message_type message,
	                                                const pf::message_params& params)
	{
		const auto owner = owner_.lock();
		if (!owner || !owner->reactor()) return 0;
		const auto frame = std::static_pointer_cast<WindowFrame>(owner);
		MessageResult result = 0;
		switch (message)
		{
		case pf::message_type::create: result = owner->reactor()->message(frame, WindowMessage::create); break;
		case pf::message_type::destroy: result = owner->reactor()->message(frame, WindowMessage::destroy); break;
		case pf::message_type::close: result = owner->reactor()->message(frame, WindowMessage::close); break;
		case pf::message_type::dpi_changed:
			result = owner->reactor()->message(frame, WindowMessage::dpiChanged);
			break;
		case pf::message_type::set_focus:
			result = owner->reactor()->message(frame, WindowMessage::focusGained);
			break;
		case pf::message_type::kill_focus:
			result = owner->reactor()->message(frame, WindowMessage::focusLost);
			break;
		case pf::message_type::timer:
			result = owner->reactor()->message(frame, WindowMessage::timer);
			break;
		case pf::message_type::drop_files:
		{
			FileDrop drop;
			drop.point = {};
			for (const auto& path : params.dropped_paths)
				drop.files.emplace_back(pf::utf8_to_utf16(path.view()));
			result = owner->reactor()->files_dropped(frame, drop);
			break;
		}
		default: break;
		}
		if (message == pf::message_type::create) owner->apply_pending();
		return static_cast<std::uint32_t>(result);
	}

	std::uint32_t LibdesktopReactor::handle_mouse(pf::window_frame_ptr,
	                                              const pf::mouse_message_type message,
	                                              const pf::mouse_params& params)
	{
		const auto owner = owner_.lock();
		if (!owner || !owner->reactor()) return 0;
		const auto frame = std::static_pointer_cast<WindowFrame>(owner);
		MouseInput input;
		input.point = {params.point.x, params.point.y};
		const auto screen = owner->client_to_screen(input.point);
		input.screenPoint = screen;
		input.leftButton = params.left_button;
		input.control = params.control;
		input.shift = params.shift;
		input.wheelDelta = params.wheel_delta;
		MouseMessage translated = MouseMessage::move;
		switch (message)
		{
		case pf::mouse_message_type::left_button_down: translated = MouseMessage::leftButtonDown; break;
		case pf::mouse_message_type::left_button_dbl_clk: translated = MouseMessage::leftButtonDoubleClick; break;
		case pf::mouse_message_type::right_button_down: translated = MouseMessage::rightButtonDown; break;
		case pf::mouse_message_type::left_button_up: translated = MouseMessage::leftButtonUp; break;
		case pf::mouse_message_type::mouse_move: translated = MouseMessage::move; break;
		case pf::mouse_message_type::mouse_wheel: translated = MouseMessage::wheel; break;
		case pf::mouse_message_type::mouse_leave: translated = MouseMessage::leave; break;
		default: translated = MouseMessage::contextMenu; break;
		}
		return static_cast<std::uint32_t>(owner->reactor()->mouse(frame, translated, input));
	}

	std::uint32_t LibdesktopReactor::handle_keyboard(pf::window_frame_ptr,
	                                                 const pf::keyboard_message_type message,
	                                                 const pf::keyboard_params& params)
	{
		const auto owner = owner_.lock();
		if (!owner || !owner->reactor() || message == pf::keyboard_message_type::key_up) return 0;
		const auto frame = std::static_pointer_cast<WindowFrame>(owner);
		KeyInput input;
		input.key = [vk = params.vk]
		{
			switch (vk)
			{
			case pf::platform_key::Left: return KeyCode::left;
			case pf::platform_key::Right: return KeyCode::right;
			case pf::platform_key::Up: return KeyCode::up;
			case pf::platform_key::Down: return KeyCode::down;
			case pf::platform_key::Home: return KeyCode::home;
			case pf::platform_key::End: return KeyCode::end;
			case pf::platform_key::Prior: return KeyCode::pageUp;
			case pf::platform_key::Next: return KeyCode::pageDown;
			case pf::platform_key::Space: return KeyCode::space;
			case pf::platform_key::Delete: return KeyCode::deleteKey;
			case pf::platform_key::F2: return KeyCode::f2;
			case pf::platform_key::Return: return KeyCode::enter;
			case pf::platform_key::Escape: return KeyCode::escape;
			case pf::platform_key::Divide: return KeyCode::divide;
			case pf::platform_key::Multiply: return KeyCode::multiply;
			case pf::platform_key::Add: return KeyCode::add;
			case pf::platform_key::Subtract: return KeyCode::subtract;
			default: return KeyCode::unknown;
			}
		}();
		input.character = params.ch <= 0xffff ? static_cast<wchar_t>(params.ch) : L'\ufffd';
		input.control = owner->native_frame()->is_key_down_async(pf::platform_key::Control);
		input.shift = owner->native_frame()->is_key_down_async(pf::platform_key::Shift);
		input.alt = owner->native_frame()->is_key_down_async(pf::platform_key::Alt);
		return static_cast<std::uint32_t>(owner->reactor()->key(frame,
			message == pf::keyboard_message_type::char_input ? KeyMessage::character : KeyMessage::down,
			input));
	}

	std::uint32_t LibdesktopReactor::handle_gesture(pf::window_frame_ptr,
	                                                const pf::gesture_params& params)
	{
		const auto owner = owner_.lock();
		if (!owner || !owner->reactor()) return 0;
		const auto frame = std::static_pointer_cast<WindowFrame>(owner);
		GestureInput input;
		input.kind = params.kind == pf::gesture_kind::zoom ? GestureKind::zoom : GestureKind::pan;
		input.point = {params.point.x, params.point.y};
		input.argument = params.argument;
		input.begin = params.begin;
		input.end = params.end;
		return static_cast<std::uint32_t>(owner->reactor()->gesture(frame, input));
	}

	void LibdesktopReactor::handle_paint(pf::window_frame_ptr&, pf::draw_context& draw)
	{
		const auto owner = owner_.lock();
		if (!owner || !owner->reactor()) return;
		LibdesktopDrawContext context(draw);
		auto frame = std::static_pointer_cast<WindowFrame>(owner);
		owner->reactor()->paint(frame, context);
	}

	void LibdesktopReactor::handle_size(pf::window_frame_ptr&, const pf::isize extent,
	                                    pf::measure_context& measure)
	{
		const auto owner = owner_.lock();
		if (!owner || !owner->reactor()) return;
		LibdesktopMeasureContext context(measure);
		auto frame = std::static_pointer_cast<WindowFrame>(owner);
		owner->reactor()->size(frame, {extent.cx, extent.cy}, context);
	}

	void show_popup_menu_pf(const WindowFramePtr& owner, const app::pointi screenPoint,
	                        const std::span<const PopupItem> items)
	{
		const auto frame = std::dynamic_pointer_cast<LibdesktopWindowFrame>(owner);
		if (!frame) return;
		std::vector<pf::menu_command> commands;
		int nextId = 1;
		for (const auto& item : items)
		{
			if (item.separator)
			{
				commands.emplace_back();
				continue;
			}
			if (!item.command) continue;
			const auto command = item.command;
			commands.emplace_back(
				pf::utf16_to_utf8(item.label), nextId++,
				[command] { if (command->invoke) command->invoke(); },
				command->enabled, command->checked);
		}
		frame->native_frame()->show_popup_menu(commands, {screenPoint.x, screenPoint.y});
	}

	namespace
	{
		pf::key_binding to_pf_key(const Shortcut shortcut)
		{
			unsigned int key = 0;
			switch (shortcut.key)
			{
			case ShortcutKey::a: key = 'A'; break;
			case ShortcutKey::b: key = 'B'; break;
			case ShortcutKey::c: key = 'C'; break;
			case ShortcutKey::e: key = 'E'; break;
			case ShortcutKey::n: key = 'N'; break;
			case ShortcutKey::o: key = 'O'; break;
			case ShortcutKey::r: key = 'R'; break;
			case ShortcutKey::v: key = 'V'; break;
			case ShortcutKey::x: key = 'X'; break;
			case ShortcutKey::z: key = 'Z'; break;
			case ShortcutKey::deleteKey: key = pf::platform_key::Delete; break;
			case ShortcutKey::enterKey: key = pf::platform_key::Return; break;
			case ShortcutKey::f1: key = pf::platform_key::F1; break;
			case ShortcutKey::f2: key = pf::platform_key::F2; break;
			case ShortcutKey::f3: key = pf::platform_key::F3; break;
			case ShortcutKey::f4: key = pf::platform_key::F4; break;
			case ShortcutKey::f5: key = pf::platform_key::F5; break;
			case ShortcutKey::f6: key = pf::platform_key::F6; break;
			case ShortcutKey::f7: key = pf::platform_key::F7; break;
			case ShortcutKey::f8: key = pf::platform_key::F8; break;
			case ShortcutKey::f9: key = pf::platform_key::F9; break;
			case ShortcutKey::f11: key = pf::platform_key::F11; break;
			case ShortcutKey::f12: key = pf::platform_key::F12; break;
			default: break;
			}
			std::uint8_t modifiers = pf::key_mod::none;
			if (shortcut.control) modifiers |= pf::key_mod::ctrl;
			if (shortcut.shift) modifiers |= pf::key_mod::shift;
			if (shortcut.alt) modifiers |= pf::key_mod::alt;
			return {key, modifiers};
		}

		pf::menu_command to_pf_menu_item(const MenuItem& item)
		{
			pf::menu_command result;
			if (item.kind == MenuItemKind::separator) return result;
			if (item.kind == MenuItemKind::submenu)
			{
				result.text = pf::utf16_to_utf8(item.name);
				for (const auto& child : item.children)
					result.children.push_back(to_pf_menu_item(child));
				return result;
			}
			if (!item.command) return result;
			const auto command = item.command;
			result.text = pf::utf16_to_utf8(command->name);
			result.id = 1;
			result.action = [command] { if (command->invoke) command->invoke(); };
			result.is_enabled = command->enabled;
			result.is_checked = command->checked;
			result.accel = to_pf_key(command->shortcut);
			return result;
		}
	}

	void set_frame_menu(const WindowFramePtr& owner, const std::vector<Menu> menus)
	{
		const auto frame = std::dynamic_pointer_cast<LibdesktopWindowFrame>(owner);
		if (!frame) return;
		std::vector<pf::menu_command> menu;
		menu.reserve(menus.size());
		for (const auto& top : menus)
		{
			pf::menu_command item;
			item.text = pf::utf16_to_utf8(top.name);
			for (const auto& child : top.items) item.children.push_back(to_pf_menu_item(child));
			menu.push_back(std::move(item));
		}
		frame->native_frame()->set_menu(std::move(menu));
	}

	pf::window_frame_ptr native_frame(const WindowFramePtr& owner)
	{
		if (const auto frame = std::dynamic_pointer_cast<LibdesktopWindowFrame>(owner))
			return frame->native_frame();
		return {};
	}

	pf::window_frame_ptr pendingMainFrame;

	void bind_main_frame(const pf::window_frame_ptr& frame)
	{
		pendingMainFrame = frame;
	}

	WindowFramePtr create_top_level_frame(FrameReactorPtr reactor, const WindowOptions& options)
	{
		if (pendingMainFrame)
		{
			auto frame = std::make_shared<LibdesktopWindowFrame>(std::exchange(pendingMainFrame, nullptr));
			frame->set_reactor(std::move(reactor));
			frame->set_title(options.title);
			frame->move(options.bounds);
			frame->show(options.visible);
			frame->set_maximized(options.maximized);
			return frame;
		}
		auto frame = std::make_shared<LibdesktopWindowFrame>(
			std::make_shared<pf::ui::test::fake_window_frame>());
		frame->set_reactor(std::move(reactor));
		frame->set_title(options.title);
		frame->move(options.bounds);
		frame->show(options.visible);
		frame->set_maximized(options.maximized);
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
		const auto bytes = pf::embedded_resource_data(name);
		const auto bitmap = bytes.empty() ? pf::bitmap_ptr{} : pf::load_bitmap_memory(bytes.data(), bytes.size());
		if (!bitmap || bitmap->empty()) return result;
		result.size = {bitmap->width, bitmap->height};
		result.pixels = bitmap->pixels;
		result.hasAlpha = std::ranges::any_of(result.pixels, [](const std::uint32_t pixel)
		{
			return (pixel >> 24) != 0xff;
		});
		if (!result.hasAlpha)
			for (auto& pixel : result.pixels) pixel |= 0xff000000;
		return result;
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

	}

	bool queue_ui(std::function<void()> work)
	{
		if (!work) return false;
		pf::run_ui(std::move(work));
		return true;
	}

	bool queue_work(const WorkQueue queue, std::function<void()> work)
	{
		return work && serial_queue(queue).push(std::move(work));
	}

	namespace
	{
		std::stop_source& exit_request()
		{
			static std::stop_source source;
			return source;
		}
	}

	std::stop_token exit_token()
	{
		return exit_request().get_token();
	}

	void shutdown_runtime()
	{
		exit_request().request_stop();
		for (const auto queue : {
			     WorkQueue::image, WorkQueue::thumbnail, WorkQueue::metadata, WorkQueue::folder,
			     WorkQueue::shell, WorkQueue::task
		     })
			serial_queue(queue).stop();
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
		df::folder_path selected;
		if (!::platform::browse_for_folder(selected)) return std::nullopt;
		return ::platform::to_stream_path(selected);
	}

	std::optional<std::filesystem::path> choose_open_file(const WindowFramePtr& owner, const OpenFileOptions& options)
	{
		std::vector<df::file_path> selected;
		const std::vector<::platform::file_dialog_filter> filters{
			{::platform::utf16_to_utf8(options.filterName), ::platform::utf16_to_utf8(options.filterPattern)},
			{"All files", "*.*"}
		};
		if (!::platform::prompt_for_open_paths(selected, filters, false) || selected.empty())
			return std::nullopt;
		return ::platform::to_stream_path(selected.front());
	}

	std::optional<std::filesystem::path> choose_save_file(const WindowFramePtr& owner, const SaveFileOptions& options)
	{
		df::file_path selected(desktop::file_path(options.initialFolder / options.initialName).pack());
		const std::vector<::platform::file_dialog_filter> filters{
			{::platform::utf16_to_utf8(options.filterName), ::platform::utf16_to_utf8(options.filterPattern)}
		};
		if (!::platform::prompt_for_save_path(selected, filters)) return std::nullopt;
		return ::platform::to_stream_path(selected);
	}

	int read_integer_setting(const std::wstring_view section, const std::wstring_view key, const int fallback)
	{
		const auto text = pf::config_read(pf::utf16_to_utf8(section), pf::utf16_to_utf8(key),
		                                  std::to_string(fallback));
		int value = fallback;
		const auto first = text.data();
		const auto last = first + text.size();
		std::from_chars(first, last, value);
		return value;
	}

	void write_integer_settings(const std::wstring_view section, const std::span<const IntegerSetting> values)
	{
		for (const auto& value : values)
			pf::config_write(pf::utf16_to_utf8(section), pf::utf16_to_utf8(value.key),
			                 std::to_string(value.value));
		pf::config_flush();
	}

	std::wstring read_text_setting(const std::wstring_view section, const std::wstring_view key,
	                               const std::wstring_view fallback)
	{
		return pf::utf8_to_utf16(pf::config_read(pf::utf16_to_utf8(section), pf::utf16_to_utf8(key),
		                                         pf::utf16_to_utf8(fallback)));
	}

	void write_text_settings(const std::wstring_view section, const std::span<const TextSetting> values)
	{
		for (const auto& value : values)
			pf::config_write(pf::utf16_to_utf8(section), pf::utf16_to_utf8(value.key),
			                 pf::utf16_to_utf8(value.value));
		pf::config_flush();
	}

	void clear_settings_section(const std::wstring_view section)
	{
		const auto sectionText = pf::utf16_to_utf8(section);
		// The shared INI API intentionally writes through and does not expose a
		// destructive section delete.  Clear the bounded list keys used by 3.0;
		// empty values preserve the portable file and make stale rows invisible.
		for (int index = 0; index < 256; ++index)
		{
			pf::config_write(sectionText, "Path" + std::to_string(index), {});
			pf::config_write(sectionText, "Collection" + std::to_string(index), {});
		}
		for (const auto key : {"WholeCollection", "CollectionCount", "WindowLeft", "WindowTop",
		                       "WindowWidth", "WindowHeight", "Maximized", "ShowFolderTree",
		                       "FolderTreeWidth", "ThumbnailSize", "ViewMode", "FitImage",
		                       "PasteImageFormat"})
			pf::config_write(sectionText, key, {});
		pf::config_flush();
	}

	std::filesystem::path module_folder()
	{
		return std::filesystem::path(pf::utf8_to_utf16(pf::file_path::module_folder().view()));
	}

	std::filesystem::path known_folder(const KnownFolder folder)
	{
		::platform::known_folder identifier = ::platform::known_folder::pictures;
		switch (folder)
		{
		case KnownFolder::documents: identifier = ::platform::known_folder::documents; break;
		case KnownFolder::desktop: identifier = ::platform::known_folder::desktop; break;
		case KnownFolder::downloads: identifier = ::platform::known_folder::downloads; break;
		case KnownFolder::music: identifier = ::platform::known_folder::music; break;
		case KnownFolder::videos: identifier = ::platform::known_folder::video; break;
		default: break;
		}
		std::filesystem::path result;
		const auto path = ::platform::known_path(identifier);
		if (!path.is_empty()) result = ::platform::to_stream_path(path);
		std::error_code error;
		if (!result.empty() && !std::filesystem::is_directory(result, error)) result.clear();
		return result;
	}

	ClipboardStatus clipboard_status()
	{
		const auto clipboard = ::platform::clipboard();
		if (!clipboard) return {};
		return {clipboard->has_drop_files(), clipboard->has_bitmap()};
	}

	bool set_clipboard_files(const WindowFramePtr& owner, const std::span<const std::filesystem::path> paths,
	                         const bool move)
	{
		if (paths.empty()) return false;
		std::vector<df::file_path> files;
		files.reserve(paths.size());
		for (const auto& path : paths) files.push_back(desktop::file_path(path));
		return ::platform::set_clipboard(files, {}, {}, move);
	}

	ClipboardContent read_clipboard(const WindowFramePtr& owner)
	{
		ClipboardContent result;
		const auto clipboard = ::platform::clipboard();
		if (!clipboard) return result;
		for (const auto& path : clipboard->drop_paths())
			result.files.push_back(::platform::to_stream_path(path));
		if (!result.files.empty())
		{
			result.operation = clipboard->files_description().preferred_drop_effect == ::platform::drop_effect::move
				                   ? FileOperation::move
				                   : FileOperation::copy;
		}
		else if (clipboard->has_bitmap())
		{
			const auto folder = ::platform::to_stream_path(::platform::temp_folder());
			const auto name = std::format(L"iw30-clipboard-{}.png", ::platform::tick_count());
			const auto saved = clipboard->save_bitmap(desktop::folder_path(folder),
			                                          desktop::utf8_path(std::filesystem::path(name)), true);
			if (saved.success())
			{
				const auto path = folder / name;
				const auto image = files::load_image(path);
				result.image.size = {image.width, image.height};
				result.image.pixels = image.pixels;
				result.image.hasAlpha = std::ranges::any_of(result.image.pixels,
					[](const std::uint32_t pixel) { return (pixel >> 24) != 0xff; });
				::platform::delete_file(desktop::file_path(path));
			}
		}
		return result;
	}

	bool perform_file_operation(const WindowFramePtr& owner, const FileOperation operation,
	                            const std::span<const std::filesystem::path> sources,
	                            const std::filesystem::path& destination)
	{
		if (sources.empty()) return false;
		std::vector<df::file_path> files;
		files.reserve(sources.size());
		for (const auto& path : sources) files.push_back(desktop::file_path(path));
		if (operation == FileOperation::recycle)
			return ::platform::delete_items(files, {}, true).success();
		if (destination.empty()) return false;
		const auto result = ::platform::move_or_copy(
			files, {}, desktop::folder_path(destination), operation == FileOperation::move, false);
		return result.success();
	}

	bool delete_permanently(const std::span<const std::filesystem::path> paths, std::error_code& error)
	{
		error.clear();
		bool allRemoved = true;
		for (const auto& path : paths)
		{
			auto result = ::platform::delete_file(desktop::file_path(path));
			if (result.failed())
			{
				::platform::make_file_writable(desktop::file_path(path));
				result = ::platform::delete_file(desktop::file_path(path));
			}
			if (result.success()) continue;
			allRemoved = false;
			if (!error) error = std::make_error_code(std::errc::io_error);
		}
		return allRemoved;
	}

	namespace
	{
		class DesktopAppHandler final : public AppHandler
		{
		public:
			DesktopAppHandler(::platform::open_with_entry entry, std::wstring name)
				: entry_(std::move(entry)), name_(std::move(name)) {}

			const std::wstring& name() const override { return name_; }

			bool invoke(const std::span<const std::filesystem::path> paths) const override
			{
				if (paths.empty() || !entry_.invoke) return false;
				std::vector<df::file_path> files;
				files.reserve(paths.size());
				for (const auto& path : paths) files.push_back(desktop::file_path(path));
				return entry_.invoke(files, {});
			}

		private:
			::platform::open_with_entry entry_;
			std::wstring name_;
		};
	}

	std::vector<AppHandlerPtr> registered_apps_for_extension(const std::wstring_view extension)
	{
		std::vector<AppHandlerPtr> result;
		if (extension.empty() || extension.front() != L'.') return result;
		const auto handlers = ::platform::assoc_handlers(pf::utf16_to_utf8(extension));
		constexpr size_t maximumHandlers = 32;
		for (const auto& handler : handlers)
		{
			result.push_back(std::make_shared<DesktopAppHandler>(
				handler, pf::utf8_to_utf16(handler.name)));
			if (result.size() >= maximumHandlers) break;
		}
		return result;
	}

	bool launch_process(const std::filesystem::path& executable, const std::wstring_view arguments,
	                    const WindowFramePtr& owner)
	{
		if (executable.empty()) return false;
		return ::platform::run(desktop::file_path(executable), pf::utf16_to_utf8(arguments));
	}

	namespace
	{
		class PfTextInput final : public TextInput
		{
		public:
			PfTextInput(const WindowFramePtr& owner, TextInputOptions options)
				: options_(std::move(options))
			{
				const auto frame = native_frame(owner);
				if (!frame) return;
				pf::text_input_options input;
				input.cue = pf::utf16_to_utf8(options_.cueBanner);
				input.changed = [this](const std::string_view value)
				{
					if (options_.changed) options_.changed(pf::utf8_to_utf16(value));
				};
				input.accepted = [this](const std::string_view)
				{
					if (options_.accepted) options_.accepted();
				};
				input.escaped = [this]
				{
					if (options_.escaped) options_.escaped();
				};
				input.tabbed = [this](const bool backwards)
				{
					if (options_.tabbed) options_.tabbed(backwards);
				};
				input_ = pf::create_text_input(frame, std::move(input));
				if (input_) input_->set_text(pf::utf16_to_utf8(options_.text));
			}

			void set_bounds(const app::recti bounds) override
			{
				if (input_) input_->set_bounds({bounds.x, bounds.y, bounds.right(), bounds.bottom()});
			}

			void show(const bool visible) override { if (input_) input_->show(visible); }
			void set_focus() override { if (input_) input_->set_focus(); }
			bool has_focus() const override { return input_ && input_->has_focus(); }
			std::wstring text() const override
			{
				return input_ ? pf::utf8_to_utf16(input_->text()) : std::wstring{};
			}
			void set_text(const std::wstring_view value) override
			{
				if (input_) input_->set_text(pf::utf16_to_utf8(value));
			}
			void set_font(const FontPtr&) override
			{
				if (input_) input_->set_font({"Segoe UI", 9});
			}

		private:
			TextInputOptions options_;
			pf::text_input_ptr input_;
		};
	}

	TextInputPtr create_text_input(const WindowFramePtr& owner, TextInputOptions options)
	{
		return std::make_shared<PfTextInput>(owner, std::move(options));
	}

	Runtime::Runtime(const RuntimeMode mode) : mode_(mode)
	{
		if (mode == RuntimeMode::multithreaded)
		{
			initialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
			return;
		}

		if (FAILED(OleInitialize(nullptr))) return;
		initialized_ = true;
		if (!initialized_) OleUninitialize();
	}

	Runtime::~Runtime()
	{
		if (!initialized_) return;
		if (mode_ == RuntimeMode::multithreaded) CoUninitialize();
		else
		{
			OleUninitialize();
		}
	}

	void show_error(const std::wstring_view message, const std::wstring_view title, const WindowFramePtr& owner)
	{
		if (const auto frame = std::dynamic_pointer_cast<LibdesktopWindowFrame>(owner))
		{
			frame->native_frame()->message_box(pf::utf16_to_utf8(message), pf::utf16_to_utf8(title),
			                                   pf::msg_box_style::ok);
			return;
		}
		::platform::show_startup_failure(
			pf::utf16_to_utf8(std::wstring(title) + L": " + std::wstring(message)));
	}

	void write_diagnostic(const std::wstring_view text)
	{
		if (diagnostics::run_log().write(text))
			::platform::trace(std::string("ImageWalker: diagnostic file write failed."));
		const auto value = ::platform::utf16_to_utf8(text);
		::platform::trace(value);
		pf::write_stdout(value);
	}

	std::filesystem::path unique_temp_folder(const std::wstring_view name)
	{
		auto folder = ::platform::to_stream_path(::platform::temp_folder());
		if (folder.empty()) return {};
		folder /= std::format(L"{}-{}", name, ::platform::process_id());
		return folder;
	}

	FileAttributes read_file_attributes(const std::filesystem::path& path)
	{
		FileAttributes result;
		std::error_code directoryError;
		const bool directory = std::filesystem::is_directory(files::native_path(path), directoryError);
		const auto attributes = directory
			                        ? ::platform::file_attributes(desktop::folder_path(path))
			                        : ::platform::file_attributes(desktop::file_path(path));
		if (!attributes.exists()) return result;
		result.known = true;
		result.hidden = attributes.is_hidden;
		result.directory = directory;
		result.reparse = attributes.is_reparse;
		return result;
	}

	bool has_sse2()
	{
		return imaging::has_sse2();
	}

	bool has_avx2()
	{
		return imaging::has_avx2();
	}

	void open_uri(const std::wstring_view uri)
	{
		::platform::open(pf::utf16_to_utf8(uri));
	}

}
