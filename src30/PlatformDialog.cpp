// ImageWalker by Zac Walker
// Adapts the application dialog model to libdesktop's neutral modal controls.

#include "PlatformWin32.h"
#include "Platform.h"
#include "Appearance.h"

#include <commctrl.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <map>

namespace iw::platform
{
	namespace
	{
		HWND configure_popup(const ::ui::frame_ptr& frame)
		{
			const auto handle = frame->handle();
			const auto* window = std::any_cast<HWND>(&handle);
			if (!window || !*window) return nullptr;
			const auto style = GetWindowLongPtrW(*window, GWL_STYLE);
			// Libdesktop already creates an owned popup; give it native move and close chrome.
			if ((style & WS_CHILD) || !GetWindow(*window, GW_OWNER)) return nullptr;
			SetWindowLongPtrW(*window, GWL_STYLE, style | WS_CAPTION | WS_SYSMENU);
			const auto extended = GetWindowLongPtrW(*window, GWL_EXSTYLE);
			SetWindowLongPtrW(*window, GWL_EXSTYLE, extended | WS_EX_DLGMODALFRAME);
			SetWindowPos(*window, nullptr, 0, 0, 0, 0,
			             SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
			win32::refresh_appearance(*window);
			return *window;
		}

		std::string drawn_label(const std::wstring_view label)
		{
			std::wstring text;
			for (size_t i = 0; i < label.size(); ++i)
			{
				if (label[i] != L'&') text += label[i];
				else if (i + 1 < label.size() && label[i + 1] == L'&')
				{
					text += L'&';
					++i;
				}
			}
			return ::core::utf16_to_utf8(text);
		}

		::platform::modal_field_kind modal_kind(const DialogFieldKind kind)
		{
			switch (kind)
			{
			case DialogFieldKind::checkBox: return ::platform::modal_field_kind::check_box;
			case DialogFieldKind::choice: return ::platform::modal_field_kind::choice;
			case DialogFieldKind::searchList: return ::platform::modal_field_kind::search_list;
			case DialogFieldKind::action: return ::platform::modal_field_kind::action;
			default: return ::platform::modal_field_kind::text;
			}
		}

		::platform::modal_value modal_value(const DialogValue& value)
		{
			if (const auto* text = std::get_if<std::wstring>(&value)) return ::core::utf16_to_utf8(*text);
			if (const auto* checked = std::get_if<bool>(&value)) return *checked;
			if (const auto* number = std::get_if<std::int64_t>(&value)) return *number;
			return std::string{};
		}

		DialogValue application_value(const ::platform::modal_value& value)
		{
			if (const auto* text = std::get_if<std::string>(&value)) return ::core::utf8_to_utf16(*text);
			if (const auto* checked = std::get_if<bool>(&value)) return *checked;
			if (const auto* number = std::get_if<std::int64_t>(&value)) return *number;
			return {};
		}

		DialogValues application_values(const std::map<std::uint32_t, ::platform::modal_value>& values)
		{
			DialogValues result;
			for (const auto& [id, value] : values) result.emplace(id, application_value(value));
			return result;
		}

		class DrawnDialog final : public ::ui::frame_host, public std::enable_shared_from_this<DrawnDialog>
		{
		public:
			explicit DrawnDialog(DialogDefinition definition) : definition_(std::move(definition)) {}

			std::optional<DialogValues> run(const ::ui::frame_ptr& owner)
			{
				frame_ = owner->create_dlg(weak_from_this());
				if (!frame_) return {};
				bool restoreOwner = false;
				const ::core::scope_exit restore([&]
				{
					frame_->destroy();
					if (restoreOwner)
					{
						owner->enable(true);
						owner->focus();
					}
				});
				hasNativeCaption_ = configure_popup(frame_) != nullptr;
				frame_->window_text(::core::utf16_to_utf8(definition_.title));
				build();
				const auto ownerBounds = owner->window_bounds();
				auto desktop = ::platform::monitor_work_area(ownerBounds);
				if (desktop.is_empty()) desktop = ::platform::desktop_bounds(true);
				if (desktop.is_empty()) return {};
				const auto center = ownerBounds.is_empty() ? desktop.center() : ownerBounds.center();
				int width = (std::min)(desktop.width(), scaled(edits_.empty() ? 464 : 536));
				const auto position = [&](const int height)
				{
					const int x = std::clamp(center.x - width / 2, desktop.left, desktop.right - width);
					const int y = std::clamp(center.y - height / 2, desktop.top, desktop.bottom - height);
					return ::core::recti{x, y, x + width, y + height};
				};
				const auto initialHeight = (std::min)(desktop.height(), scaled(320));
				frame_->window_bounds(position(initialHeight), false);
				// Moving to the owner's monitor can change the DPI and the caption's height.
				width = (std::min)(desktop.width(), scaled(edits_.empty() ? 464 : 536));
				maxBodyHeight_ = (std::max)(scaled(48), desktop.height() / (edits_.empty() ? 3 : 5));
				frame_->window_bounds(position(initialHeight), false);
				frame_->layout();
				const int nonClient = (std::max)(0, initialHeight - extent_.cy);
				frame_->window_bounds(position(std::clamp(height_ + nonClient, 1, desktop.height())), false);
				restoreOwner = owner->is_enabled();
				if (restoreOwner) owner->enable(false);
				frame_->show(true);
				frame_->layout();
				if (definition_.cancelText.empty()) accept_->focus();
				else frame_->focus_first();
				const auto close = frame_->wait_for_close(0);
				return close == ::ui::close_result::ok ? std::move(result_) : std::nullopt;
			}

			void on_window_layout(render::measure_context& mc, const ::core::sizei extent, bool minimized) override
			{
				if (minimized) return;
				extent_ = extent;
				const int padding = scaled(16);
				const int gap = scaled(6);
				const int width = (std::max)(1, extent.cx - 2 * padding);
				int y = padding;
				const auto place = [&](const ::ui::element_ptr& element)
				{
					const int height = element->measure(mc, width).cy;
					element->layout(mc, {padding, y, padding + width, y + height});
					y += height + gap;
				};
				if (title_) place(title_);
				if (body_)
				{
					const int h = body_->measure(mc, width).cy;
					const int shown = (std::min)(h, maxBodyHeight_);
					bodyClip_ = {padding, y, padding + width, y + shown};
					body_->layout(mc, {padding, y, padding + width, y + h});
					bodyMaximum_ = h - shown;
					y += shown + gap;
				}
				for (const auto& element : elements_) place(element);
				if (error_)
				{
					const int h = mc.text_line_height(render::font_face::dialog);
					error_->layout(mc, {padding, y, padding + width, y + h});
					y += h;
				}
				y += gap;
				const auto accept = accept_->measure(mc, width);
				const auto cancel = cancel_ ? cancel_->measure(mc, width) : ::core::sizei{};
				const int buttonHeight = (std::max)(accept.cy, cancel.cy);
				const int right = padding + width;
				if (cancel_) cancel_->layout(mc, {right - cancel.cx, y, right, y + buttonHeight});
				const int acceptRight = right - (cancel_ ? cancel.cx + gap : 0);
				accept_->layout(mc, {acceptRight - accept.cx, y, acceptRight, y + buttonHeight});
				height_ = y + buttonHeight + padding;
			}

			void on_window_paint(render::draw_context& dc) override
			{
				dc.colors = frame_colors();
				dc.draw_rect(::core::recti(extent_), ::core::color(dc.colors.background, 1.0f));
				if (title_) title_->render(dc, {});
				if (body_)
				{
					dc.clip_bounds(bodyClip_);
					body_->render(dc, {0, -bodyScroll_});
					dc.restore_clip();
				}
				for (const auto& element : elements_) element->render(dc, {});
				if (error_) error_->render(dc, {});
				accept_->render(dc, {});
				if (cancel_) cancel_->render(dc, {});
			}

			void visit_elements(const std::function<void(const ::ui::element_ptr&, ::core::pointi)>& visit) override
			{
				for (const auto& element : elements_) visit(element, {});
				visit(accept_, {});
				if (cancel_) visit(cancel_, {});
			}

			bool key_down(const char32_t key, const ::platform::key_state keys) override
			{
				if (keys.control || keys.alt) return false;
				if (key == ::platform::keys::ESCAPE)
				{
					frame_->close(true);
					return true;
				}
				if (key == ::platform::keys::RETURN)
				{
					accept();
					return true;
				}
				if (key == ::platform::keys::PRIOR) return scroll_body(-bodyClip_.height());
				if (key == ::platform::keys::NEXT) return scroll_body(bodyClip_.height());
				return false;
			}

			void on_mouse_wheel(::core::pointi, int delta, ::platform::key_state, bool& handled) override
			{
				handled = scroll_body(-delta / 2);
			}

		private:
			int scaled(const int value) const { return (std::max)(1, ::core::round(value * frame_->scale_factor())); }

			bool scroll_body(const int delta)
			{
				if (!bodyMaximum_) return false;
				bodyScroll_ = std::clamp(bodyScroll_ + delta, 0, bodyMaximum_);
				frame_->invalidate(bodyClip_);
				return true;
			}

			void update_values()
			{
				for (const auto& [id, edit] : edits_) values_[id] = ::core::utf8_to_utf16(edit->window_text());
			}

			void accept()
			{
				update_values();
				if (definition_.validate)
					if (const auto error = definition_.validate(values_))
					{
						error_->text(::core::utf16_to_utf8(*error));
						frame_->invalidate();
						return;
					}
				result_ = values_;
				frame_->close();
			}

			void build()
			{
				if (!hasNativeCaption_)
					title_ = std::make_shared<::ui::text_element>(::core::utf16_to_utf8(definition_.title),
					                                              render::font_face::title);
				if (!definition_.message.empty())
					body_ = std::make_shared<::ui::text_element>(::core::utf16_to_utf8(definition_.message),
					                                             render::text_style::multiline);
				for (const auto& field : definition_.fields)
				{
					const auto label = drawn_label(field.label);
					values_[field.id] = field.value;
					switch (field.kind)
					{
					case DialogFieldKind::text:
					{
						::ui::edit_styles style;
						style.file_system_auto_complete = field.folderCompletion;
						style.select_all_on_focus = true;
						if (field.folderCompletion) style.cue = "Type a folder path";
						const auto* text = std::get_if<std::wstring>(&field.value);
						const auto edit = ::ui::make_edit(style, text ? ::core::utf16_to_utf8(*text) : std::string{},
						                                  {});
						if (!field.label.empty()) elements_.push_back(std::make_shared<::ui::text_element>(label));
						edit->enable(field.enabled);
						edits_[field.id] = edit;
						elements_.push_back(edit);
						break;
					}
					case DialogFieldKind::action:
						elements_.push_back(::ui::make_button(label,
						                                      [this, action = field.invoke, change = field.editValues]
						{
							if (change)
							{
								update_values();
								change(values_);
								for (const auto& [id, edit] : edits_)
									if (const auto* text = std::get_if<std::wstring>(&values_.at(id)))
										edit->window_text(::core::utf16_to_utf8(*text));
							}
							else if (action) action();
						}));
						elements_.back()->enable(field.enabled);
						break;
					case DialogFieldKind::choice:
					{
						if (!field.label.empty()) elements_.push_back(std::make_shared<::ui::text_element>(label));
						const auto selected = std::get_if<std::int64_t>(&field.value);
						for (const auto& option : field.choices)
						{
							elements_.push_back(
								::ui::make_check_button(selected && *selected == option.value,
							                            drawn_label(option.label), true,
							                            [this, id = field.id, value = option.value](const bool checked)
							{
								if (checked) values_[id] = value;
							}, static_cast<int>(field.id)));
							elements_.back()->enable(field.enabled);
						}
						break;
					}
					case DialogFieldKind::checkBox:
						elements_.push_back(
							::ui::make_check_button(std::get_if<bool>(&field.value) && std::get<bool>(field.value),
						                            label, false, [this, id = field.id](const bool checked)
						{ values_[id] = checked; }));
						elements_.back()->enable(field.enabled);
						break;
					default: break;
					}
				}
				if (definition_.validate) error_ = std::make_shared<::ui::text_element>("");
				accept_ = ::ui::make_button(::core::utf16_to_utf8(definition_.acceptText), [this] { accept(); }, true);
				if (!definition_.cancelText.empty())
					cancel_ = ::ui::make_button(::core::utf16_to_utf8(definition_.cancelText),
					                            [this] { frame_->close(true); });
			}

			DialogDefinition definition_;
			::ui::frame_ptr frame_;
			std::shared_ptr<::ui::text_element> title_, body_, error_;
			std::vector<::ui::element_ptr> elements_;
			std::map<DialogFieldId, ::ui::edit_ptr> edits_;
			::ui::button_ptr accept_, cancel_;
			DialogValues values_;
			std::optional<DialogValues> result_;
			::core::sizei extent_;
			::core::recti bodyClip_;
			int height_{1};
			int maxBodyHeight_{};
			int bodyMaximum_{};
			int bodyScroll_{};
			bool hasNativeCaption_{};
		};
	}

	std::optional<DialogValues> show_modal_dialog(const WindowFramePtr& owner, DialogDefinition definition)
	{
		if (::platform::is_agent_session()) return {};
		const auto frame = native_ui_frame(owner);
		if (frame && definition.previews.empty() &&
		    std::ranges::none_of(definition.fields,
		                         [](const DialogField& field) { return field.kind == DialogFieldKind::searchList; }))
			return std::make_shared<DrawnDialog>(std::move(definition))->run(frame);

		::platform::modal_definition modal;
		modal.title = ::core::utf16_to_utf8(definition.title);
		modal.message = ::core::utf16_to_utf8(definition.message);
		modal.accept = ::core::utf16_to_utf8(definition.acceptText);
		modal.cancel = ::core::utf16_to_utf8(definition.cancelText);
		for (const auto& preview : definition.previews)
			modal.previews.push_back({preview.size.width, preview.size.height, preview.pixels});
		for (const auto& field : definition.fields)
		{
			::platform::modal_field item;
			item.id = field.id;
			item.kind = modal_kind(field.kind);
			item.label = ::core::utf16_to_utf8(field.label);
			item.value = modal_value(field.value);
			item.enabled = field.enabled;
			item.action = field.invoke;
			for (const auto& choice : field.choices)
				item.choices.emplace_back(::core::utf16_to_utf8(choice.label), choice.value);
			modal.fields.push_back(std::move(item));
		}
		modal.validate =
			[validate = std::move(definition.validate)](
				const std::map<std::uint32_t, ::platform::modal_value>& values) -> std::optional<std::string>
		{
			if (!validate) return {};
			const auto message = validate(application_values(values));
			return message ? std::optional<std::string>(::core::utf16_to_utf8(*message)) : std::nullopt;
		};
		const auto values = ::platform::show_modal_dialog(nullptr, std::move(modal));
		return values ? std::optional<DialogValues>(application_values(*values)) : std::nullopt;
	}

	int show_choice(const WindowFramePtr& owner, const ChoiceDefinition& definition)
	{
		if (::platform::is_agent_session() || definition.buttons.empty()) return 0;
		const auto hwnd = win32::owner_window(owner);
		TASKDIALOGCONFIG dialog{sizeof(TASKDIALOGCONFIG)};
		dialog.hwndParent = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
		dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
		dialog.pszWindowTitle = definition.title.c_str();
		dialog.pszMainInstruction = definition.heading.c_str();
		dialog.pszContent = definition.message.c_str();
		dialog.pszMainIcon = definition.warning ? TD_WARNING_ICON : TD_INFORMATION_ICON;
		// Native Escape/X returns IDCANCEL (2), which is a valid, sometimes destructive application button ID.
		constexpr int firstButton = 100;
		std::vector<TASKDIALOG_BUTTON> buttons;
		for (const auto& button : definition.buttons)
		{
			const auto id = firstButton + static_cast<int>(buttons.size());
			if (button.id == definition.defaultButton) dialog.nDefaultButton = id;
			buttons.push_back({id, button.label.c_str()});
		}
		dialog.pButtons = buttons.data();
		dialog.cButtons = static_cast<UINT>(buttons.size());
		int result{};
		if (FAILED(TaskDialogIndirect(&dialog, &result, nullptr, nullptr)) || result < firstButton) return 0;
		const auto index = static_cast<size_t>(result - firstButton);
		return index < definition.buttons.size() ? definition.buttons[index].id : 0;
	}

	bool run_with_status(const WindowFramePtr&, std::wstring_view, std::wstring_view, std::function<bool()> work)
	{
		// The platform dialog does not support an owned status dialog; this pumps the task queue instead.
		auto completion = std::make_shared<std::promise<bool>>();
		auto result = completion->get_future();
		::platform::run_async([work = std::move(work), completion]() mutable
		{
			try
			{
				completion->set_value(work ? work() : false);
			}
			catch (...)
			{
				completion->set_exception(std::current_exception());
			}
		});
		while (result.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
			::platform::pump_ui_tasks(10);
		return result.get();
	}
}
