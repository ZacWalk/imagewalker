// ImageWalker by Zac Walker
// Draws the command surface through shared ui child frames and render contracts.

#include "Platform.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <memory>
#include <vector>

namespace iw::platform
{
	std::wstring toolbar_label(const Command& command)
	{
		if (command.toolbarText) return command.toolbarText();

		const auto& name = command.name;
		std::wstring label;
		label.reserve(name.size());
		for (size_t i = 0; i < name.size(); ++i)
		{
			if (name[i] == L'&' && i + 1 < name.size())
			{
				const auto next = name[i + 1];
				if (next == L'&')
				{
					label += L'&';
					++i;
					continue;
				}
				// A marker begins a word, as in "&Refresh" and "Folder &tree"; "R&B" keeps its ampersand.
				if ((i == 0 || !std::iswalnum(name[i - 1])) && std::iswalnum(next)) continue;
			}
			label += name[i];
		}
		return label;
	}

	MenuItem MenuItem::action(CommandPtr command)
	{
		MenuItem result;
		result.command = std::move(command);
		return result;
	}

	MenuItem MenuItem::submenu(std::wstring label, std::vector<MenuItem> items)
	{
		MenuItem result;
		result.kind = MenuItemKind::submenu;
		result.name = std::move(label);
		result.children = std::move(items);
		return result;
	}

	MenuItem MenuItem::separator()
	{
		MenuItem result;
		result.kind = MenuItemKind::separator;
		return result;
	}

	ToolbarItem ToolbarItem::action(CommandPtr command)
	{
		ToolbarItem result;
		result.command = std::move(command);
		return result;
	}

	ToolbarItem ToolbarItem::menu(CommandPtr command, std::vector<MenuItem> items)
	{
		ToolbarItem result;
		result.kind = ToolbarItemKind::menu;
		result.command = std::move(command);
		result.menuItems = std::move(items);
		return result;
	}

	ToolbarItem ToolbarItem::separator()
	{
		ToolbarItem result;
		result.kind = ToolbarItemKind::separator;
		return result;
	}

	namespace
	{
		int scale_metric(const int value, const unsigned int dpi)
		{
			return value * static_cast<int>(dpi ? dpi : 96) / 96;
		}

		class ChromeSurface;

		class ChromeReactor final : public FrameReactor
		{
		public:
			explicit ChromeReactor(std::weak_ptr<ChromeSurface> owner) : owner_(std::move(owner)) {}

			MessageResult mouse(const WindowFramePtr& frame, MouseMessage message, const MouseInput& input) override;
			MessageResult message(const WindowFramePtr& frame, WindowMessage message) override;
			void paint(const WindowFramePtr& frame, DrawContext& draw) override;
			void size(const WindowFramePtr&, app::sizei, MeasureContext&) override {}

		private:
			std::weak_ptr<ChromeSurface> owner_;
		};

		struct Hit
		{
			app::recti bounds;
			CommandPtr command;
			std::vector<MenuItem> menu;
			std::filesystem::path breadcrumb;
			bool folderChoices{};
		};

		class ChromeSurface final : public CommandSurface, public std::enable_shared_from_this<ChromeSurface>
		{
		public:
			explicit ChromeSurface(WindowFramePtr owner) : owner_(std::move(owner)) {}

			bool initialize()
			{
				if (!owner_) return false;
				WindowOptions options;
				options.className = "ImageWalker30.CommandSurface";
				options.child = true;
				options.visible = true;
				options.eraseBackground = false;
				top_ = owner_->create_child(std::make_shared<ChromeReactor>(weak_from_this()), options);
				statusFrame_ = owner_->create_child(std::make_shared<ChromeReactor>(weak_from_this()), options);
				set_dpi(owner_->dpi());
				return top_ && statusFrame_;
			}

			void set_menu(std::vector<Menu> menus) override
			{
				menus_ = std::move(menus);
				set_frame_menu(owner_, menus_);
			}

			void set_toolbar(std::vector<ToolbarItem> items) override
			{
				toolbar_ = std::move(items);
				invalidate();
			}

			void set_task_toolbar(std::vector<ToolbarItem> items) override
			{
				++folderMenuGeneration_;
				taskToolbar_ = std::move(items);
				taskActive_ = true;
				invalidate();
			}

			void clear_task_toolbar() override
			{
				if (!taskActive_) return;
				taskToolbar_.clear();
				taskActive_ = false;
				invalidate();
			}

			void set_status_toolbar(std::vector<ToolbarItem> items) override
			{
				statusToolbar_ = std::move(items);
				invalidate();
			}

			void set_breadcrumbs(std::vector<Breadcrumb> breadcrumbs,
			                     std::function<void(const std::filesystem::path&)> selected) override
			{
				breadcrumbs_ = std::move(breadcrumbs);
				++folderMenuGeneration_;
				breadcrumbSelected_ = std::move(selected);
				invalidate();
			}

			void set_status(StatusInfo status) override
			{
				statusInfo_ = std::move(status);
				invalidate();
			}

			void set_thumbnail_size(const int value, std::function<void(int)> changed) override
			{
				thumbnailSize_ = value;
				thumbnailChanged_ = std::move(changed);
				invalidate();
			}

			app::recti layout(const app::recti client, const bool fullscreen) override
			{
				if (fullscreen_ != fullscreen)
				{
					fullscreen_ = fullscreen;
					set_frame_menu(owner_, fullscreen ? std::vector<Menu>{} : menus_);
				}
				if (!top_ || !statusFrame_) return client;
				if (fullscreen)
				{
					top_->show(false);
					statusFrame_->show(false);
					return client;
				}
				const int menuHeight = menus_.empty() ? 0 : scale_metric(24, dpi_);
				const int topHeight = (std::min)(scale_metric(38, dpi_), (std::max)(0, client.height - menuHeight));
				const int statusHeight = (std::min)(scale_metric(32, dpi_),
				                                    (std::max)(0, client.height - menuHeight - topHeight));
				top_->move({client.x, client.y + menuHeight, client.width, topHeight});
				statusFrame_->move({client.x, client.bottom() - statusHeight, client.width, statusHeight});
				top_->show(true);
				statusFrame_->show(true);
				top_->invalidate();
				statusFrame_->invalidate();
				return {client.x, client.y + menuHeight + topHeight, client.width,
				        (std::max)(0, client.height - menuHeight - topHeight - statusHeight)};
			}

			void set_dpi(const unsigned int dpi) override
			{
				const auto effective = dpi ? dpi : 96;
				if (dpi_ == effective) return;
				dpi_ = effective;
				font_ = create_message_font(dpi_);
				iconFont_ = command_icon_font(dpi_);
				invalidate();
			}

			void refresh() override
			{
				set_frame_menu(owner_, fullscreen_ ? std::vector<Menu>{} : menus_);
				invalidate();
			}

			void paint(const WindowFramePtr& frame, DrawContext& draw)
			{
				if (frame == top_) paint_top(draw);
				else if (frame == statusFrame_) paint_status(draw);
			}

			MessageResult mouse(const WindowFramePtr& frame, const MouseMessage message, const MouseInput& input)
			{
				if (frame == statusFrame_ && sliderDragging_ &&
				    (message == MouseMessage::move || message == MouseMessage::leftButtonUp))
				{
					change_thumbnail_size(input.point.x);
					if (message == MouseMessage::leftButtonUp)
					{
						sliderDragging_ = false;
						frame->release_capture();
					}
					return 1;
				}
				if (message == MouseMessage::leave)
				{
					hoverCommand_.reset();
					hoverBreadcrumb_ = {};
					invalidate();
					return 0;
				}
				if (message != MouseMessage::leftButtonDown && message != MouseMessage::move) return 0;
				auto& hits = frame == top_ ? topHits_ : statusHits_;
				if (message == MouseMessage::move)
				{
					frame->track_mouse_leave();
					const auto hit = hit_test(hits, input.point);
					const auto command = hit ? hit->command : CommandPtr{};
					const auto breadcrumb = frame == top_ && hit && !hit->command ? hit->bounds : app::recti{};
					if (command != hoverCommand_ || breadcrumb != hoverBreadcrumb_)
					{
						hoverCommand_ = command;
						hoverBreadcrumb_ = breadcrumb;
						invalidate();
					}
					return 0;
				}
				const auto found = hit_test(hits, input.point);
				if (!found) return 0;
				const auto hit = *found;
				if (frame == statusFrame_ && !hit.command && thumbnailChanged_ && !taskActive_)
				{
					sliderDragging_ = true;
					frame->set_capture();
					change_thumbnail_size(input.point.x);
					return 1;
				}
				if (hit.folderChoices)
				{
					show_folder_choices(hit.breadcrumb, hit.bounds);
					return 1;
				}
				if (!hit.breadcrumb.empty())
				{
					if (breadcrumbSelected_) breadcrumbSelected_(hit.breadcrumb);
					return 0;
				}
				if (hit.command && hit.command->enabled && !hit.command->enabled()) return 0;
				if (!hit.menu.empty())
				{
					std::vector<PopupItem> popup;
					for (const auto& item : hit.menu) append_popup(popup, item);
					show_popup_menu(frame, frame->client_to_screen({hit.bounds.x, hit.bounds.bottom()}), popup);
				}
				else if (hit.command && hit.command->invoke) hit.command->invoke();
				return 0;
			}

			void capture_lost() { sliderDragging_ = false; }

		private:
			void change_thumbnail_size(const int x)
			{
				if (!thumbnailChanged_ || sliderBounds_.width <= 1) return;
				const int offset = std::clamp(x - sliderBounds_.x, 0, sliderBounds_.width - 1);
				const int value = 64 + offset * 192 / (sliderBounds_.width - 1);
				if (value == thumbnailSize_) return;
				thumbnailSize_ = value;
				const auto changed = thumbnailChanged_;
				changed(value);
				statusFrame_->invalidate();
			}

			void show_folder_choices(const std::filesystem::path& path, const app::recti anchor)
			{
				const auto generation = ++folderMenuGeneration_;
				const auto weak = weak_from_this();
				if (!queue_work(WorkQueue::shell, [weak, path, anchor, generation]
				{
					auto choices = child_folders(path);
					queue_ui([weak, anchor, generation, choices = std::move(choices)]
					{
						const auto self = weak.lock();
						if (!self || generation != self->folderMenuGeneration_ || self->taskActive_ ||
						    self->fullscreen_)
							return;
						std::vector<PopupItem> menu;
						if (choices)
							for (const auto& child : *choices)
							{
								auto command = std::make_shared<Command>();
								command->name = child.label;
								command->invoke = [select = self->breadcrumbSelected_, folder = child.path]
								{
									if (select) select(folder);
								};
								menu.push_back(PopupItem::action(std::move(command), child.label));
							}
						if (menu.empty())
						{
							auto empty = std::make_shared<Command>();
							empty->name = choices ? L"No subfolders" : L"Could not read this folder";
							empty->enabled = []
							{
								return false;
							};
							const auto text = empty->name;
							menu.push_back(PopupItem::action(std::move(empty), text));
						}
						show_popup_menu(self->top_, self->top_->client_to_screen({anchor.x, anchor.bottom()}), menu);
					});
				}))
					write_diagnostic(L"The folder menu worker is unavailable.\n");
			}

			static const Hit* hit_test(const std::vector<Hit>& hits, const app::pointi point)
			{
				for (const auto& hit : hits)
					if (hit.bounds.contains(point)) return &hit;
				return nullptr;
			}

			static void append_popup(std::vector<PopupItem>& result, const MenuItem& item)
			{
				if (item.kind == MenuItemKind::separator)
				{
					result.push_back(PopupItem::separator_item());
					return;
				}
				if (item.kind == MenuItemKind::submenu)
				{
					for (const auto& child : item.children) append_popup(result, child);
					return;
				}
				if (item.command) result.push_back(PopupItem::action(item.command, item.command->name));
			}

			void invalidate()
			{
				if (top_) top_->invalidate();
				if (statusFrame_) statusFrame_->invalidate();
			}

			const std::vector<ToolbarItem>& active_toolbar() const { return taskActive_ ? taskToolbar_ : toolbar_; }

			static constexpr auto centered_text = TextFormat::center | TextFormat::verticalCenter |
			                                      TextFormat::singleLine | TextFormat::endEllipsis;

			static wchar_t symbol_code(const CommandBitmap bitmap)
			{
				using enum CommandBitmap;
				switch (bitmap)
				{
				case forward: return L'\uE72A';
				case back: return L'\uE72B';
				case refresh: return L'\uE72C';
				case folderTree: return L'\uE8B7';
				case openFolder: return L'\uE838';
				case about: return L'\uE946';
				case details: return L'\uE8FD';
				case thumbnails: return L'\uE91B';
				case matrix: return L'\uE80A';
				case photo:
				case photosOnly: return L'\uE8B9';
				case sort: return L'\uE8CB';
				case breadcrumbChevron: return L'\uE76C';
				case fit: return L'\uE799';
				case zoomIn: return L'\uE8A3';
				case zoomOut: return L'\uE71F';
				default: return 0;
				}
			}

			void draw_symbol(DrawContext& draw, const CommandBitmap bitmap, const app::recti box, const color ink) const
			{
				if (const auto glyph = symbol_code(bitmap); glyph && !box.is_empty())
					draw.text(std::wstring_view(&glyph, 1), box, ink, centered_text, iconFont_);
			}

			int command_width(DrawContext& draw, const ToolbarItem& item) const
			{
				const auto& command = *item.command;
				const bool icon = symbol_code(command.bitmap) != 0;
				int width = scale_metric(icon ? 34 : 16, dpi_);
				if (!icon || command.toolbarText)
					width += draw.measure_text(toolbar_label(command), TextFormat::singleLine, font_).width;
				if (item.kind == ToolbarItemKind::menu) width += scale_metric(13, dpi_);
				return width;
			}

			void paint_command(DrawContext& draw, const ToolbarItem& item, app::recti bounds) const
			{
				const auto& command = *item.command;
				const bool enabled = !command.enabled || command.enabled();
				const auto ink = system_color(enabled ? SystemColor::buttonText : SystemColor::grayText);
				if (enabled && command.checked && command.checked())
					draw.fill(bounds, system_color(SystemColor::checked));
				else if (enabled && hoverCommand_ == item.command) draw.fill(bounds, system_color(SystemColor::hover));
				if (item.kind == ToolbarItemKind::menu)
				{
					const int arrowWidth = scale_metric(13, dpi_);
					draw.text(L"\u25BE", {bounds.right() - arrowWidth, bounds.y, arrowWidth, bounds.height}, ink,
					          centered_text, font_);
					bounds.width -= arrowWidth;
				}
				if (symbol_code(command.bitmap))
				{
					const int iconWidth = scale_metric(34, dpi_);
					draw_symbol(draw, command.bitmap, {bounds.x, bounds.y, iconWidth, bounds.height}, ink);
					bounds.x += iconWidth;
					bounds.width -= iconWidth;
					if (!command.toolbarText) return;
				}
				if (bounds.width > 0) draw.text(toolbar_label(command), bounds, ink, centered_text, font_);
			}

			void paint_breadcrumbs(DrawContext& draw, const app::recti field)
			{
				const int divider = scale_metric(22, dpi_);
				if (field.width < divider * 2 || breadcrumbs_.empty()) return;
				const auto ink = system_color(SystemColor::windowText);
				const auto muted = system_color(SystemColor::grayText);
				const auto width_of = [&](const Breadcrumb& part)
				{
					return (std::min)(scale_metric(150, dpi_),
					                  draw.measure_text(part.label, TextFormat::singleLine, font_).width +
					                      scale_metric(14, dpi_));
				};
				const int overflow = scale_metric(28, dpi_);
				const int available = field.width - divider - scale_metric(4, dpi_);
				size_t first = breadcrumbs_.size() - 1;
				const int leafWidth = (std::min)(width_of(breadcrumbs_.back()), available);
				int needed = leafWidth;
				while (first > 0)
				{
					const int preceding = width_of(breadcrumbs_[first - 1]) + divider;
					const int reserve = first > 1 ? overflow : 0;
					if (needed + preceding + reserve > available) break;
					needed += preceding;
					--first;
				}

				int x = field.x;
				if (first > 0 && available - leafWidth >= overflow)
				{
					const app::recti hit{x, field.y + 1, overflow, field.height - 2};
					if (hoverBreadcrumb_ == hit) draw.fill(hit, system_color(SystemColor::hover));
					draw.text(L"\u2026", hit, ink, centered_text, font_);
					std::vector<MenuItem> ancestors;
					for (size_t i = 0; i < first; ++i)
					{
						auto command = std::make_shared<Command>();
						command->name = breadcrumbs_[i].label;
						command->invoke = [select = breadcrumbSelected_, path = breadcrumbs_[i].path]
						{
							if (select) select(path);
						};
						ancestors.push_back(MenuItem::action(std::move(command)));
					}
					topHits_.push_back({hit, {}, std::move(ancestors), {}});
					x += overflow;
				}
				for (size_t i = first; i < breadcrumbs_.size(); ++i)
				{
					const bool current = i + 1 == breadcrumbs_.size();
					const auto& part = breadcrumbs_[i];
					const int width = (std::min)(width_of(part), field.right() - x - divider - scale_metric(4, dpi_));
					const app::recti hit{x, field.y + 1, width, field.height - 2};
					if (!current && hoverBreadcrumb_ == hit) draw.fill(hit, system_color(SystemColor::hover));
					draw.text(part.label, hit, current ? ink : muted, centered_text, font_);
					if (!current) topHits_.push_back({hit, {}, {}, part.path});
					const app::recti split{hit.right(), field.y + 1, divider, field.height - 2};
					if (hoverBreadcrumb_ == split) draw.fill(split, system_color(SystemColor::hover));
					draw_symbol(draw, CommandBitmap::breadcrumbChevron, split, ink);
					topHits_.push_back({split, {}, {}, part.path, true});
					x += width + divider;
				}
			}

			void paint_top(DrawContext& draw)
			{
				const auto bounds = top_->client_rect();
				const auto chromeBackground = system_color(SystemColor::face);
				draw.fill(bounds, chromeBackground);
				topHits_.clear();
				int x = scale_metric(4, dpi_);
				for (const auto& item : active_toolbar())
				{
					if (item.kind == ToolbarItemKind::separator)
					{
						x += scale_metric(8, dpi_);
						continue;
					}
					if (!item.command) continue;
					const auto width = command_width(draw, item);
					const app::recti hit{x, 0, width, bounds.height};
					paint_command(draw, item, hit);
					topHits_.push_back({hit, item.command, item.menuItems, {}});
					x += width;
				}
				if (!taskActive_ && !breadcrumbs_.empty())
				{
					x += scale_metric(8, dpi_);
					const int margin = scale_metric(4, dpi_);
					paint_breadcrumbs(draw, {x, margin, bounds.width - x - margin, bounds.height - margin * 2});
				}
			}

			void paint_status(DrawContext& draw)
			{
				const auto bounds = statusFrame_->client_rect();
				const auto chromeBackground = system_color(SystemColor::face);
				const auto chromeText = system_color(SystemColor::buttonText);
				draw.fill(bounds, chromeBackground);
				statusHits_.clear();
				const int sliderWidth = taskActive_ || bounds.width <= scale_metric(530, dpi_)
				                            ? 0
				                            : std::clamp(bounds.width - scale_metric(578, dpi_), scale_metric(52, dpi_),
				                                         scale_metric(112, dpi_));
				int x = bounds.width - sliderWidth - scale_metric(8, dpi_);
				if (!taskActive_)
					for (auto it = statusToolbar_.rbegin(); it != statusToolbar_.rend(); ++it)
					{
						const auto& item = *it;
						if (item.kind == ToolbarItemKind::separator)
						{
							x -= scale_metric(8, dpi_);
							continue;
						}
						if (!item.command) continue;
						const int width = command_width(draw, item);
						x -= width;
						const app::recti hit{x, 0, width, bounds.height};
						paint_command(draw, item, hit);
						statusHits_.push_back({hit, item.command, item.menuItems, {}});
					}
				const int margin = scale_metric(7, dpi_);
				sliderBounds_ = {};
				if (sliderWidth)
				{
					const app::recti slider{bounds.width - sliderWidth, 0, sliderWidth - margin, bounds.height};
					sliderBounds_ = slider;
					const int thumb = scale_metric(8, dpi_);
					const int center = (slider.width - 1) * (std::clamp(thumbnailSize_, 64, 256) - 64) / (256 - 64);
					draw.fill({slider.x, bounds.height / 2 - 1, slider.width, 2}, system_color(SystemColor::shadow));
					draw.fill({slider.x + center - thumb / 2, bounds.height / 2 - thumb, thumb, thumb * 2},
					          system_color(SystemColor::highlight));
					statusHits_.push_back({slider, {}, {}, {}});
				}
				const int progressWidth = !statusInfo_.scanning && statusInfo_.progressPercent < 100 &&
				                                  x > scale_metric(200, dpi_)
				                              ? scale_metric(85, dpi_)
				                              : 0;
				if (progressWidth > 0)
				{
					const app::recti bar{x - progressWidth - margin, bounds.height / 2 - 3, progressWidth, 6};
					draw.outline(bar, system_color(SystemColor::shadow));
					draw.fill({bar.x, bar.y, bar.width * std::clamp(statusInfo_.progressPercent, 0, 100) / 100,
					           bar.height},
					          system_color(SystemColor::highlight));
				}
				const auto hint = hoverCommand_ ? (hoverCommand_->tooltip.empty() ? toolbar_label(*hoverCommand_)
				                                                                  : hoverCommand_->tooltip)
				                                : statusInfo_.text;
				draw.text(hint, {margin, 0, (std::max)(0, x - progressWidth - margin * 2), bounds.height}, chromeText,
				          TextFormat::left | TextFormat::verticalCenter | TextFormat::singleLine |
				              TextFormat::endEllipsis,
				          font_);
			}

			WindowFramePtr owner_;
			WindowFramePtr top_;
			WindowFramePtr statusFrame_;
			FontPtr font_;
			FontPtr iconFont_;
			std::vector<Menu> menus_;
			std::vector<ToolbarItem> toolbar_;
			std::vector<ToolbarItem> taskToolbar_;
			std::vector<ToolbarItem> statusToolbar_;
			std::vector<Breadcrumb> breadcrumbs_;
			std::function<void(const std::filesystem::path&)> breadcrumbSelected_;
			std::function<void(int)> thumbnailChanged_;
			StatusInfo statusInfo_;
			std::vector<Hit> topHits_;
			std::vector<Hit> statusHits_;
			CommandPtr hoverCommand_;
			app::recti hoverBreadcrumb_;
			app::recti sliderBounds_;
			std::uint64_t folderMenuGeneration_{};
			int thumbnailSize_{128};
			unsigned int dpi_{};
			bool taskActive_{};
			bool fullscreen_{};
			bool sliderDragging_{};
		};

		MessageResult ChromeReactor::mouse(const WindowFramePtr& frame, const MouseMessage message,
		                                   const MouseInput& input)
		{
			if (const auto owner = owner_.lock()) return owner->mouse(frame, message, input);
			return 0;
		}

		MessageResult ChromeReactor::message(const WindowFramePtr&, const WindowMessage message)
		{
			if (message == WindowMessage::captureLost)
				if (const auto owner = owner_.lock()) owner->capture_lost();
			return 0;
		}

		void ChromeReactor::paint(const WindowFramePtr& frame, DrawContext& draw)
		{
			if (const auto owner = owner_.lock()) owner->paint(frame, draw);
		}
	}

	CommandSurfacePtr create_command_surface(const WindowFramePtr& owner)
	{
		if (!owner) return {};
		auto result = std::make_shared<ChromeSurface>(owner);
		return result->initialize() ? result : CommandSurfacePtr{};
	}
}
