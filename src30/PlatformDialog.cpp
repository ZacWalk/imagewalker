// ImageWalker by Zac Walker
// Implements dynamically created and laid-out native Windows dialogs.

#include "Platform.h"
#include "PlatformWin32.h"

#include <commctrl.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>
#include <set>

namespace iw::platform
{
	namespace
	{
		constexpr int firstFieldControl = 2000;
		constexpr wchar_t dialogClassName[] = L"ImageWalker30.DynamicDialog";
		constexpr DWORD dialogStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
		constexpr DWORD dialogExtendedStyle = WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME;

		int scale_for_dpi(const int value, const unsigned int dpi)
		{
			return MulDiv(value, static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI),
			              USER_DEFAULT_SCREEN_DPI);
		}

		unsigned int window_dpi(const HWND window)
		{
			using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
			static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
				GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
			return getDpiForWindow && window ? getDpiForWindow(window) : USER_DEFAULT_SCREEN_DPI;
		}

		void set_control_font(const HWND control, const FontPtr& font)
		{
			if (control && font)
				SendMessageW(control, WM_SETFONT, font->native_handle(), TRUE);
		}

		struct FieldControl
		{
			const DialogField* field{};
			HWND label{};
			HWND input{};
			HWND search{};
		};

		class WinDynamicDialog
		{
		public:
			WinDynamicDialog(const HWND owner, DialogDefinition definition)
				: owner_(owner), definition_(std::move(definition)), dpi_(window_dpi(owner)),
				  font_(create_message_font(dpi_))
			{
			}

			std::optional<DialogValues> show()
			{
				if (definition_.fields.size() > static_cast<size_t>(UINT16_MAX - firstFieldControl))
					return std::nullopt;
				std::set<DialogFieldId> fieldIds;
				for (const auto& field : definition_.fields)
					if (!fieldIds.insert(field.id).second) return std::nullopt;
				if (!register_window_class()) return std::nullopt;
				dialog_ = CreateWindowExW(
					dialogExtendedStyle, dialogClassName, definition_.title.c_str(), dialogStyle,
					CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, owner_, nullptr, GetModuleHandleW(nullptr), this);
				if (!dialog_) return std::nullopt;

				const bool restoreOwner = owner_ && IsWindow(owner_) && IsWindowEnabled(owner_);
				if (restoreOwner) EnableWindow(owner_, FALSE);
				ShowWindow(dialog_, SW_SHOW);
				UpdateWindow(dialog_);

				MSG message{};
				bool quitRequested{};
				int quitCode{};
				while (dialog_)
				{
					if (!win32::wait_for_message()) break;
					while (dialog_ && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
					{
						if (message.message == WM_QUIT)
						{
							quitRequested = true;
							quitCode = static_cast<int>(message.wParam);
							break;
						}
						if (!IsDialogMessageW(dialog_, &message))
						{
							TranslateMessage(&message);
							DispatchMessageW(&message);
						}
					}
					if (quitRequested) break;
				}

				if (dialog_) DestroyWindow(dialog_);
				if (restoreOwner && IsWindow(owner_))
				{
					EnableWindow(owner_, TRUE);
					SetActiveWindow(owner_);
				}
				if (quitRequested) PostQuitMessage(quitCode);
				return accepted_ ? std::optional<DialogValues>(std::move(values_)) : std::nullopt;
			}

		private:
			static bool register_window_class()
			{
				WNDCLASSEXW windowClass{sizeof(windowClass)};
				windowClass.lpfnWndProc = window_proc;
				windowClass.hInstance = GetModuleHandleW(nullptr);
				windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				windowClass.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
				windowClass.lpszClassName = dialogClassName;
				return RegisterClassExW(&windowClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
			}

			static LRESULT CALLBACK window_proc(const HWND window, const UINT message,
			                                    const WPARAM wparam, const LPARAM lparam) noexcept
			{
				try
				{
					auto* self = reinterpret_cast<WinDynamicDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
					if (message == WM_NCCREATE)
					{
						self = static_cast<WinDynamicDialog*>(
							reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
						self->dialog_ = window;
						SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
					}
					if (!self) return DefWindowProcW(window, message, wparam, lparam);
					if (message == WM_CREATE)
					{
						self->initialize();
						return 0;
					}
					if (message == WM_PAINT && !self->definition_.previews.empty())
					{
						PAINTSTRUCT paint{};
						const HDC dc = BeginPaint(window, &paint);
						FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_3DFACE));
						const int edge = scale_for_dpi(64, self->dpi_);
						const int gap = scale_for_dpi(8, self->dpi_);
						const int margin = scale_for_dpi(14, self->dpi_);
						int x = margin;
						for (const auto& preview : self->definition_.previews)
						{
							if (preview.size.width <= 0 || preview.size.height <= 0 ||
								preview.pixels.size() < static_cast<size_t>(preview.size.width) * preview.size.height)
								continue;
							const double scale = (std::min)(static_cast<double>(edge) / preview.size.width,
								static_cast<double>(edge) / preview.size.height);
							const int width = (std::max)(1, static_cast<int>(preview.size.width * scale));
							const int height = (std::max)(1, static_cast<int>(preview.size.height * scale));
							BITMAPINFO info{};
							info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
							info.bmiHeader.biWidth = preview.size.width;
							info.bmiHeader.biHeight = -preview.size.height;
							info.bmiHeader.biPlanes = 1;
							info.bmiHeader.biBitCount = 32;
							StretchDIBits(dc, x + (edge - width) / 2, margin + (edge - height) / 2, width, height,
								0, 0, preview.size.width, preview.size.height, preview.pixels.data(), &info,
								DIB_RGB_COLORS, SRCCOPY);
							x += edge + gap;
						}
						EndPaint(window, &paint);
						return 0;
					}
					if (message == DM_GETDEFID)
						return MAKELRESULT(self->defaultButtonId_, DC_HASDEFID);
					if (message == DM_SETDEFID)
					{
						self->set_default_button(static_cast<int>(wparam));
						return TRUE;
					}
					if (message == WM_COMMAND)
					{
						const int controlIndex = LOWORD(wparam) - firstFieldControl;
						if (controlIndex >= 0 && static_cast<size_t>(controlIndex) < self->controls_.size())
						{
							const auto& controls = self->controls_[controlIndex];
							const auto& field = *controls.field;
							if (field.kind == DialogFieldKind::searchList)
							{
								if (HIWORD(wparam) == EN_CHANGE &&
									reinterpret_cast<HWND>(lparam) == controls.search)
								{
									self->filter_list(controls);
									return 0;
								}
								if (HIWORD(wparam) == LBN_DBLCLK &&
									reinterpret_cast<HWND>(lparam) == controls.input)
								{
									if (self->accept())
									{
										self->accepted_ = true;
										DestroyWindow(window);
									}
									return 0;
								}
							}
							if (field.kind == DialogFieldKind::action && (field.invoke || field.editValues) &&
								HIWORD(wparam) == BN_CLICKED && reinterpret_cast<HWND>(lparam) == controls.input)
							{
								if (field.editValues)
								{
									auto values = self->read_values();
									field.editValues(values);
									for (const auto& target : self->controls_)
										if (target.field->kind == DialogFieldKind::text)
											if (const auto found = values.find(target.field->id); found != values.end())
												if (const auto value = std::get_if<std::wstring>(&found->second))
													SetWindowTextW(target.input, value->c_str());
								}
								else field.invoke();
								return 0;
							}
						}
						if (LOWORD(wparam) == IDOK)
						{
							if (self->accept())
							{
								self->accepted_ = true;
								DestroyWindow(window);
							}
							return 0;
						}
						if (LOWORD(wparam) == IDCANCEL)
						{
							DestroyWindow(window);
							return 0;
						}
					}
					if (message == WM_CLOSE)
					{
						DestroyWindow(window);
						return 0;
					}
					if (message == WM_DPICHANGED)
					{
						self->dpi_ = HIWORD(wparam);
						self->font_ = create_message_font(self->dpi_);
						set_control_font(window, self->font_);
						set_control_font(self->message_, self->font_);
						set_control_font(self->accept_, self->font_);
						set_control_font(self->cancel_, self->font_);
						for (const auto& controls : self->controls_)
						{
							set_control_font(controls.label, self->font_);
							set_control_font(controls.input, self->font_);
							set_control_font(controls.search, self->font_);
						}
						self->layout();
						return 0;
					}
					if (message == WM_NCDESTROY)
					{
						SetWindowLongPtrW(window, GWLP_USERDATA, 0);
						self->dialog_ = nullptr;
					}
				}
				catch (...)
				{
					DestroyWindow(window);
					return 0;
				}
				return DefWindowProcW(window, message, wparam, lparam);
			}

			HWND create_control(const DWORD extendedStyle, const wchar_t* className,
			                    const wchar_t* text, const DWORD style, const int identifier) const
			{
				const HWND control = CreateWindowExW(
					extendedStyle, className, text, WS_CHILD | WS_VISIBLE | style,
					0, 0, 0, 0, dialog_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
					GetModuleHandleW(nullptr), nullptr);
				set_control_font(control, font_);
				return control;
			}

			void set_default_button(const int identifier)
			{
				const HWND next = GetDlgItem(dialog_, identifier);
				if (!next || !(SendMessageW(next, WM_GETDLGCODE, 0, 0) &
					(DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)))
					return;
				if (const HWND previous = GetDlgItem(dialog_, defaultButtonId_); previous && previous != next)
					SendMessageW(previous, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
				SendMessageW(next, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
				defaultButtonId_ = identifier;
			}

			void initialize()
			{
				SetWindowTextW(dialog_, definition_.title.c_str());
				set_control_font(dialog_, font_);
				const COLORREF background = GetSysColor(COLOR_3DFACE);
				for (auto& preview : definition_.previews)
					if (preview.hasAlpha)
						for (auto& pixel : preview.pixels)
						{
							const auto alpha = pixel >> 24;
							const auto blend = [alpha](const unsigned channel, const unsigned backdrop)
							{
								return (channel * alpha + backdrop * (255 - alpha) + 127) / 255;
							};
							pixel = 0xff000000u | blend(pixel & 255, GetBValue(background)) |
								blend(pixel >> 8 & 255, GetGValue(background)) << 8 |
								blend(pixel >> 16 & 255, GetRValue(background)) << 16;
						}
				if (!definition_.message.empty())
					message_ = create_control(0, L"STATIC", definition_.message.c_str(), SS_LEFT, 0);

				controls_.reserve(definition_.fields.size());
				for (size_t index = 0; index < definition_.fields.size(); ++index)
				{
					const auto& field = definition_.fields[index];
					FieldControl controls{&field};
					const int identifier = firstFieldControl + static_cast<int>(index);
					if (field.kind == DialogFieldKind::checkBox)
					{
						controls.input = create_control(0, L"BUTTON", field.label.c_str(),
						                                BS_AUTOCHECKBOX | WS_TABSTOP, identifier);
						if (const auto checked = std::get_if<bool>(&field.value))
							SendMessageW(controls.input, BM_SETCHECK, *checked ? BST_CHECKED : BST_UNCHECKED, 0);
					}
					else if (field.kind == DialogFieldKind::action)
					{
						controls.input = create_control(0, L"BUTTON", field.label.c_str(),
						                                BS_PUSHBUTTON | WS_TABSTOP, identifier);
					}
					else
					{
						controls.label = create_control(0, L"STATIC", field.label.c_str(), SS_LEFT, 0);
						if (field.kind == DialogFieldKind::searchList)
						{
							controls.search = create_control(WS_EX_CLIENTEDGE, L"EDIT", L"",
								ES_AUTOHSCROLL | WS_TABSTOP, identifier);
							SendMessageW(controls.search, EM_SETCUEBANNER, TRUE,
								reinterpret_cast<LPARAM>(L"Search apps and tools"));
							controls.input = create_control(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
								LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, identifier);
							filter_list(controls);
						}
						else if (field.kind == DialogFieldKind::choice)
						{
							controls.input = create_control(0, L"COMBOBOX", L"",
							                                CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL |
							                                WS_TABSTOP, identifier);
							int selected = -1;
							const auto initial = std::get_if<std::int64_t>(&field.value);
							for (size_t choice = 0; choice < field.choices.size(); ++choice)
							{
								SendMessageW(controls.input, CB_ADDSTRING, 0,
								             reinterpret_cast<LPARAM>(field.choices[choice].label.c_str()));
								if (initial && field.choices[choice].value == *initial)
									selected = static_cast<int>(choice);
							}
							if (selected < 0 && !field.choices.empty()) selected = 0;
							SendMessageW(controls.input, CB_SETCURSEL, selected, 0);
						}
						else
						{
							const auto initial = std::get_if<std::wstring>(&field.value);
							controls.input = create_control(
								WS_EX_CLIENTEDGE, L"EDIT", initial ? initial->c_str() : L"",
								ES_AUTOHSCROLL | WS_TABSTOP, identifier);
							SendMessageW(controls.input, EM_SETSEL, field.selectionStart, field.selectionEnd);
							if (field.folderCompletion)
								SHAutoComplete(controls.input, SHACF_FILESYS_DIRS | SHACF_AUTOSUGGEST_FORCE_ON);
						}
					}
					EnableWindow(controls.input, field.enabled);
					if (controls.search) EnableWindow(controls.search, field.enabled);
					controls_.push_back(controls);
				}

				accept_ = create_control(0, L"BUTTON", definition_.acceptText.c_str(),
				                         BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
				if (!definition_.cancelText.empty())
					cancel_ = create_control(0, L"BUTTON", definition_.cancelText.c_str(),
					                         BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
				layout();
				const auto focus = std::ranges::find_if(controls_, [](const FieldControl& controls)
				{
					return controls.input && IsWindowEnabled(controls.input);
				});
				SetFocus(focus != controls_.end() ? (focus->search ? focus->search : focus->input) : accept_);
			}

			void filter_list(const FieldControl& controls)
			{
				const auto lowercase = [](std::wstring text)
				{
					std::ranges::transform(text, text.begin(), [](const wchar_t c)
					{
						return static_cast<wchar_t>(std::towlower(c));
					});
					return text;
				};
				const int length = GetWindowTextLengthW(controls.search);
				std::wstring search(static_cast<size_t>(length) + 1, L'\0');
				GetWindowTextW(controls.search, search.data(), length + 1);
				search.resize(length);
				search = lowercase(std::move(search));
				SendMessageW(controls.input, LB_RESETCONTENT, 0, 0);
				int selected = -1;
				const auto initial = std::get_if<std::int64_t>(&controls.field->value);
				for (size_t index = 0; index < controls.field->choices.size(); ++index)
				{
					const auto& choice = controls.field->choices[index];
					if (lowercase(choice.label).find(search) == std::wstring::npos) continue;
					const LRESULT position = SendMessageW(controls.input, LB_ADDSTRING, 0,
						reinterpret_cast<LPARAM>(choice.label.c_str()));
					if (position < 0) continue;
					SendMessageW(controls.input, LB_SETITEMDATA, static_cast<WPARAM>(position),
						static_cast<LPARAM>(index));
					if (initial && choice.value == *initial) selected = static_cast<int>(position);
				}
				if (selected < 0 && SendMessageW(controls.input, LB_GETCOUNT, 0, 0) > 0) selected = 0;
				SendMessageW(controls.input, LB_SETCURSEL, selected, 0);
			}

			int measure_message_height(const int width) const
			{
				if (definition_.message.empty()) return 0;
				const HDC dc = GetDC(dialog_);
				const HGDIOBJ oldFont = font_
					                        ? SelectObject(dc, reinterpret_cast<HFONT>(font_->native_handle()))
					                        : nullptr;
				RECT bounds{0, 0, width, 0};
				DrawTextW(dc, definition_.message.c_str(), -1, &bounds, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
				if (oldFont) SelectObject(dc, oldFont);
				ReleaseDC(dialog_, dc);
				return bounds.bottom - bounds.top;
			}

			void layout()
			{
				const int margin = scale_for_dpi(14, dpi_);
				const int gap = scale_for_dpi(8, dpi_);
				const int rowHeight = scale_for_dpi(26, dpi_);
				const int labelWidth = scale_for_dpi(145, dpi_);
				const int inputWidth = scale_for_dpi(220, dpi_);
				const int contentWidth = labelWidth + gap + inputWidth;
				const int buttonWidth = scale_for_dpi(80, dpi_);
				const int buttonHeight = scale_for_dpi(26, dpi_);
				int top = margin;
				if (!definition_.previews.empty()) top += scale_for_dpi(72, dpi_);
				if (message_)
				{
					const int height = measure_message_height(contentWidth);
					MoveWindow(message_, margin, top, contentWidth, height, TRUE);
					top += height + scale_for_dpi(14, dpi_);
				}
				for (const auto& controls : controls_)
				{
					if (controls.field->kind == DialogFieldKind::searchList)
					{
						const int listHeight = scale_for_dpi(170, dpi_);
						MoveWindow(controls.label, margin, top, contentWidth, rowHeight, TRUE);
						top += rowHeight + gap;
						MoveWindow(controls.search, margin, top, contentWidth, rowHeight, TRUE);
						top += rowHeight + gap;
						MoveWindow(controls.input, margin, top, contentWidth, listHeight, TRUE);
						top += listHeight + gap;
						continue;
					}
					if (controls.field->kind == DialogFieldKind::checkBox ||
						controls.field->kind == DialogFieldKind::action)
						MoveWindow(controls.input, margin, top, contentWidth, rowHeight, TRUE);
					else
					{
						MoveWindow(controls.label, margin, top + scale_for_dpi(5, dpi_), labelWidth,
						           rowHeight, TRUE);
						const int controlHeight = controls.field->kind == DialogFieldKind::choice
							                          ? scale_for_dpi(220, dpi_)
							                          : rowHeight;
						MoveWindow(controls.input, margin + labelWidth + gap, top, inputWidth,
						           controlHeight, TRUE);
					}
					top += rowHeight + gap;
				}
				top += scale_for_dpi(6, dpi_);
				const int right = margin + contentWidth;
				if (cancel_)
				{
					MoveWindow(cancel_, right - buttonWidth, top, buttonWidth, buttonHeight, TRUE);
					MoveWindow(accept_, right - gap - buttonWidth * 2, top, buttonWidth, buttonHeight, TRUE);
				}
				else MoveWindow(accept_, right - buttonWidth, top, buttonWidth, buttonHeight, TRUE);
				const int clientWidth = contentWidth + margin * 2;
				const int clientHeight = top + buttonHeight + margin;
				RECT windowBounds{0, 0, clientWidth, clientHeight};
				AdjustWindowRectEx(&windowBounds, dialogStyle, FALSE, dialogExtendedStyle);
				const int width = windowBounds.right - windowBounds.left;
				const int height = windowBounds.bottom - windowBounds.top;
				RECT ownerBounds{};
				if (!owner_ || !GetWindowRect(owner_, &ownerBounds))
				{
					ownerBounds.left = 0;
					ownerBounds.top = 0;
					ownerBounds.right = GetSystemMetrics(SM_CXSCREEN);
					ownerBounds.bottom = GetSystemMetrics(SM_CYSCREEN);
				}
				const int centeredLeft = ownerBounds.left + ((ownerBounds.right - ownerBounds.left) - width) / 2;
				const int centeredTop = ownerBounds.top + ((ownerBounds.bottom - ownerBounds.top) - height) / 2;
				MONITORINFO monitor{sizeof(monitor)};
				GetMonitorInfoW(MonitorFromRect(&ownerBounds, MONITOR_DEFAULTTONEAREST), &monitor);
				const int workLeft = static_cast<int>(monitor.rcWork.left);
				const int workTop = static_cast<int>(monitor.rcWork.top);
				const int workRight = static_cast<int>(monitor.rcWork.right);
				const int workBottom = static_cast<int>(monitor.rcWork.bottom);
				const int workWidth = workRight - workLeft;
				const int workHeight = workBottom - workTop;
				const int left = workWidth <= width
					                 ? workLeft
					                 : std::clamp(centeredLeft, workLeft, workRight - width);
				const int windowTop = workHeight <= height
					                      ? workTop
					                      : std::clamp(centeredTop, workTop, workBottom - height);
				SetWindowPos(dialog_, nullptr, left, windowTop, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
			}

			DialogValues read_values() const
			{
				DialogValues values;
				for (const auto& controls : controls_)
				{
					const auto& field = *controls.field;
					if (field.kind == DialogFieldKind::action) continue;
					if (field.kind == DialogFieldKind::checkBox)
						values[field.id] = SendMessageW(controls.input, BM_GETCHECK, 0, 0) == BST_CHECKED;
					else if (field.kind == DialogFieldKind::searchList)
					{
						const LRESULT selected = SendMessageW(controls.input, LB_GETCURSEL, 0, 0);
						const LRESULT index = selected >= 0
							? SendMessageW(controls.input, LB_GETITEMDATA, static_cast<WPARAM>(selected), 0) : -1;
						values[field.id] = index >= 0 && static_cast<size_t>(index) < field.choices.size()
							? DialogValue(field.choices[static_cast<size_t>(index)].value)
							: DialogValue(std::monostate{});
					}
					else if (field.kind == DialogFieldKind::choice)
					{
						const LRESULT selected = SendMessageW(controls.input, CB_GETCURSEL, 0, 0);
						values[field.id] = selected >= 0 && static_cast<size_t>(selected) < field.choices.size()
							                    ? DialogValue(field.choices[selected].value)
							                    : DialogValue(std::monostate{});
					}
					else
					{
						const int length = GetWindowTextLengthW(controls.input);
						std::wstring value(static_cast<size_t>(length) + 1, L'\0');
						GetWindowTextW(controls.input, value.data(), length + 1);
						value.resize(length);
						values[field.id] = std::move(value);
					}
				}
				return values;
			}

			bool accept()
			{
				values_ = read_values();
				if (definition_.validate)
				{
					if (const auto error = definition_.validate(values_))
					{
						MessageBoxW(dialog_, error->c_str(), definition_.title.c_str(), MB_OK | MB_ICONWARNING);
						return false;
					}
				}
				return true;
			}

			HWND owner_{};
			HWND dialog_{};
			DialogDefinition definition_;
			unsigned int dpi_{};
			FontPtr font_;
			HWND message_{};
			std::vector<FieldControl> controls_;
			HWND accept_{};
			HWND cancel_{};
			int defaultButtonId_{IDOK};
			DialogValues values_;
			bool accepted_{};
		};
	}

	std::optional<DialogValues> show_modal_dialog(const WindowFramePtr& owner, DialogDefinition definition)
	{
		return WinDynamicDialog(win32::owner_window(owner), std::move(definition)).show();
	}

	int show_choice(const WindowFramePtr& owner, const ChoiceDefinition& definition)
	{
		if (definition.buttons.empty()) return 0;
		std::vector<TASKDIALOG_BUTTON> buttons;
		buttons.reserve(definition.buttons.size());
		for (const auto& button : definition.buttons)
			buttons.push_back({button.id, button.label.c_str()});

		TASKDIALOGCONFIG config{sizeof(config)};
		config.hwndParent = win32::owner_window(owner);
		config.hInstance = GetModuleHandleW(nullptr);
		config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
		config.pszWindowTitle = definition.title.c_str();
		config.pszMainIcon = definition.warning ? TD_WARNING_ICON : TD_INFORMATION_ICON;
		if (!definition.heading.empty()) config.pszMainInstruction = definition.heading.c_str();
		if (!definition.message.empty()) config.pszContent = definition.message.c_str();
		config.cButtons = static_cast<UINT>(buttons.size());
		config.pButtons = buttons.data();
		config.nDefaultButton = definition.defaultButton ? definition.defaultButton : definition.buttons.front().id;

		int chosen = 0;
		if (FAILED(TaskDialogIndirect(&config, &chosen, nullptr, nullptr))) return 0;
		// Dismissing with Escape or the close box reports IDCANCEL, which is not one of ours.
		const bool known = std::ranges::any_of(definition.buttons, [chosen](const ChoiceButton& button)
		{
			return button.id == chosen;
		});
		return known ? chosen : 0;
	}
}

#include <atomic>
#include <exception>

namespace iw::platform
{
	bool run_with_status(const WindowFramePtr& owner, const std::wstring_view title,
	                     const std::wstring_view message, std::function<bool()> work)
	{
		if (!work) return false;
		struct Completion
		{
			HANDLE event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
			std::atomic_bool finished{};
			bool result{};
			std::exception_ptr error;
			~Completion() { if (event) CloseHandle(event); }
		};
		const auto completion = std::make_shared<Completion>();
		if (!completion->event) return false;

		const HWND nativeOwner = win32::owner_window(owner);
		const HWND root = nativeOwner ? GetAncestor(nativeOwner, GA_ROOT) : nullptr;
		const auto dpi = window_dpi(root);
		const auto font = root ? create_message_font(dpi) : FontPtr{};
		struct StatusWindow
		{
			HWND owner{};
			HWND window{};
			bool restoreOwner{};
			~StatusWindow()
			{
				if (window && IsWindow(window)) DestroyWindow(window);
				if (restoreOwner && IsWindow(owner))
				{
					EnableWindow(owner, TRUE);
					SetActiveWindow(owner);
				}
			}
		} status{root};
		if (root)
		{
			constexpr wchar_t className[] = L"ImageWalker30.SaveStatus";
			WNDCLASSEXW description{sizeof(description)};
			description.hInstance = GetModuleHandleW(nullptr);
			description.hCursor = LoadCursorW(nullptr, IDC_WAIT);
			description.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
			description.lpszClassName = className;
			description.lpfnWndProc = [](HWND window, UINT id, WPARAM wparam, LPARAM lparam) -> LRESULT
			{
				if (id == WM_CLOSE || (id == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_CLOSE)) return 0;
				return DefWindowProcW(window, id, wparam, lparam);
			};
			if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
			const int margin = scale_for_dpi(20, dpi);
			const int width = scale_for_dpi(420, dpi);
			const int height = scale_for_dpi(130, dpi);
			RECT bounds{};
			GetWindowRect(root, &bounds);
			const std::wstring titleText(title);
			status.window = CreateWindowExW(WS_EX_DLGMODALFRAME, className, titleText.c_str(),
				WS_POPUP | WS_CAPTION | WS_CLIPCHILDREN, bounds.left + ((bounds.right - bounds.left) - width) / 2,
				bounds.top + ((bounds.bottom - bounds.top) - height) / 2, width, height, root, nullptr,
				GetModuleHandleW(nullptr), nullptr);
			if (!status.window) return false;
			const std::wstring messageText(message);
			const HWND label = CreateWindowExW(0, L"STATIC", messageText.c_str(),
				WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, margin, margin, width - margin * 2,
				scale_for_dpi(40, dpi), status.window, nullptr, GetModuleHandleW(nullptr), nullptr);
			set_control_font(label, font);
			const HWND progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
				WS_CHILD | WS_VISIBLE | PBS_MARQUEE, margin, scale_for_dpi(68, dpi), width - margin * 2,
				scale_for_dpi(12, dpi), status.window, nullptr, GetModuleHandleW(nullptr), nullptr);
			if (!label || !progress) return false;
			SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);
			status.restoreOwner = IsWindowEnabled(root) != FALSE;
			if (status.restoreOwner) EnableWindow(root, FALSE);
			ShowWindow(status.window, SW_SHOW);
			UpdateWindow(status.window);
		}

		struct WorkItem
		{
			std::shared_ptr<Completion> completion;
			std::function<bool()> work;
			std::atomic_bool claimed{};

			void finish()
			{
				completion->finished.store(true, std::memory_order_release);
				SetEvent(completion->event);
			}

			void run()
			{
				if (claimed.exchange(true)) return;
				try { completion->result = work(); }
				catch (...) { completion->error = std::current_exception(); }
				finish();
			}

			void cancel(std::exception_ptr error = {})
			{
				if (claimed.exchange(true)) return;
				completion->error = std::move(error);
				finish();
			}

			~WorkItem() { cancel(); }
		};
		auto item = std::make_shared<WorkItem>();
		item->completion = completion;
		item->work = std::move(work);
		try
		{
			if (!queue_work(WorkQueue::task, [item] { item->run(); })) item->cancel();
		}
		catch (...) { item->cancel(std::current_exception()); }
		// If shutdown drops queued work, destruction signals failure rather than stranding the
		// waiter. A started work item, however, always owns its completion through the last write.
		item.reset();

		bool quitRequested = false;
		int quitCode = 0;
		while (!completion->finished.load(std::memory_order_acquire))
		{
			const auto waited = MsgWaitForMultipleObjectsEx(1, &completion->event, INFINITE, QS_ALLINPUT,
				MWMO_INPUTAVAILABLE);
			if (waited == WAIT_OBJECT_0) continue;
			if (waited == WAIT_FAILED)
			{
				// The owned worker must finish even when the modal message wait fails.
				WaitForSingleObject(completion->event, INFINITE);
				continue;
			}
			MSG next{};
			for (int count = 0; count < 64 && PeekMessageW(&next, nullptr, 0, 0, PM_REMOVE); ++count)
			{
				if (next.message == WM_QUIT)
				{
					quitRequested = true;
					quitCode = static_cast<int>(next.wParam);
					continue;
				}
				TranslateMessage(&next);
				DispatchMessageW(&next);
			}
		}
		if (quitRequested) PostQuitMessage(quitCode);
		if (completion->error) std::rethrow_exception(completion->error);
		return completion->result;
	}
}
