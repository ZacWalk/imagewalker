// ImageWalker by Zac Walker
// Draws the command surface through the shared pf frame and draw contracts.

#include "Platform.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace iw::platform
{
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
		constexpr color chromeBackground{245, 245, 245};
		constexpr color chromeText{32, 32, 32};
		constexpr color chromeDisabled{130, 130, 130};
		constexpr color chromeHover{218, 232, 247};
		constexpr color chromeChecked{202, 220, 241};
		constexpr color chromeBorder{190, 190, 190};

		int scale_metric(const int value, const unsigned int dpi)
		{
			return value * static_cast<int>(dpi ? dpi : 96) / 96;
		}

		class ChromeSurface;

		class ChromeReactor final : public FrameReactor
		{
		public:
			explicit ChromeReactor(std::weak_ptr<ChromeSurface> owner) : owner_(std::move(owner)) {}

			MessageResult mouse(const WindowFramePtr& frame, MouseMessage message,
			                    const MouseInput& input) override;
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
		};

		class ChromeSurface final : public CommandSurface,
		                            public std::enable_shared_from_this<ChromeSurface>
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
				fullscreen_ = fullscreen;
				set_frame_menu(owner_, fullscreen ? std::vector<Menu>{} : menus_);
				if (!top_ || !statusFrame_) return client;
				if (fullscreen)
				{
					top_->show(false);
					statusFrame_->show(false);
					return client;
				}
				const int topHeight = (std::min)(scale_metric(36, dpi_), (std::max)(0, client.height));
				const int statusHeight = (std::min)(
					scale_metric(28, dpi_), (std::max)(0, client.height - topHeight));
				top_->move({client.x, client.y, client.width, topHeight});
				statusFrame_->move({client.x, client.bottom() - statusHeight, client.width, statusHeight});
				top_->show(true);
				statusFrame_->show(true);
				top_->invalidate();
				statusFrame_->invalidate();
				return {client.x, client.y + topHeight, client.width,
				        (std::max)(0, client.height - topHeight - statusHeight)};
			}

			void set_dpi(const unsigned int dpi) override
			{
				const auto effective = dpi ? dpi : 96;
				if (dpi_ == effective) return;
				dpi_ = effective;
				font_ = create_message_font(dpi_);
				invalidate();
			}

			void refresh() override { invalidate(); }

			void paint(const WindowFramePtr& frame, DrawContext& draw)
			{
				if (frame == top_) paint_top(draw);
				else if (frame == statusFrame_) paint_status(draw);
			}

			MessageResult mouse(const WindowFramePtr& frame, const MouseMessage message,
			                    const MouseInput& input)
			{
				if (message != MouseMessage::leftButtonDown && message != MouseMessage::move) return 0;
				auto& hits = frame == top_ ? topHits_ : statusHits_;
				if (message == MouseMessage::move)
				{
					const auto hit = hit_test(hits, input.point);
					const auto command = hit ? hit->command : CommandPtr{};
					if (command != hoverCommand_) { hoverCommand_ = command; frame->invalidate(); }
					return 0;
				}
				const auto hit = hit_test(hits, input.point);
				if (!hit) return 0;
				if (frame == statusFrame_ && !hit->command && thumbnailChanged_)
				{
					const int value = std::clamp(
						64 + (input.point.x - hit->bounds.x) * (256 - 64) /
							(std::max)(1, hit->bounds.width), 64, 256);
					thumbnailSize_ = value;
					thumbnailChanged_(value);
					frame->invalidate();
					return 0;
				}
				if (!hit->breadcrumb.empty())
				{
					if (breadcrumbSelected_) breadcrumbSelected_(hit->breadcrumb);
					return 0;
				}
				if (hit->command)
				{
					if (!hit->command->enabled || hit->command->enabled())
					{
						if (!hit->menu.empty())
						{
							std::vector<PopupItem> popup;
							for (const auto& item : hit->menu) append_popup(popup, item);
							show_popup_menu(frame, input.screenPoint, popup);
						}
						else if (hit->command->invoke) hit->command->invoke();
					}
				}
				return 0;
			}

		private:
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

			const std::vector<ToolbarItem>& active_toolbar() const
			{
				return taskActive_ ? taskToolbar_ : toolbar_;
			}

			int command_width(DrawContext& draw, const CommandPtr& command) const
			{
				if (!command) return scale_metric(8, dpi_);
				const auto text = command->toolbarText ? command->toolbarText() : command->name;
				return (std::max)(scale_metric(56, dpi_),
				                  draw.measure_text(text, TextFormat::singleLine, font_).width +
				                  scale_metric(20, dpi_));
			}

			void paint_top(DrawContext& draw)
			{
				const auto bounds = top_->client_rect();
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
					const auto width = command_width(draw, item.command);
					const app::recti hit{x, 0, width, bounds.height};
					const bool enabled = !item.command->enabled || item.command->enabled();
					const bool checked = item.command->checked && item.command->checked();
					const bool hovered = hoverCommand_ == item.command;
					if (checked) draw.fill(hit, chromeChecked);
					else if (hovered) draw.fill(hit, chromeHover);
					if (!enabled) draw.fill(hit, chromeBackground);
					const auto text = item.command->toolbarText ? item.command->toolbarText() : item.command->name;
					draw.text(text, hit, enabled ? chromeText : chromeDisabled,
					          TextFormat::center | TextFormat::verticalCenter | TextFormat::singleLine |
					          TextFormat::endEllipsis, font_);
					topHits_.push_back({hit, item.command, item.menuItems, {}});
					x += width;
				}
				if (!taskActive_ && !breadcrumbs_.empty())
				{
					x += scale_metric(8, dpi_);
					for (const auto& breadcrumb : breadcrumbs_)
					{
						const auto width = draw.measure_text(
							breadcrumb.label, TextFormat::singleLine, font_).width + scale_metric(18, dpi_);
						const app::recti hit{x, 0, width, bounds.height};
						draw.text(breadcrumb.label, hit, chromeText,
						          TextFormat::center | TextFormat::verticalCenter | TextFormat::singleLine |
						          TextFormat::endEllipsis, font_);
						topHits_.push_back({hit, {}, {}, breadcrumb.path});
						x += width;
					}
				}
			}

			void paint_status(DrawContext& draw)
			{
				const auto bounds = statusFrame_->client_rect();
				draw.fill(bounds, chromeBackground);
				statusHits_.clear();
				draw.text(statusInfo_.text, {scale_metric(7, dpi_), 0, bounds.width - scale_metric(180, dpi_),
				                         bounds.height}, chromeText,
				          TextFormat::left | TextFormat::verticalCenter | TextFormat::singleLine |
				          TextFormat::endEllipsis, font_);
				if (!statusInfo_.scanning && statusInfo_.progressPercent < 100)
				{
					const int width = scale_metric(140, dpi_);
					const app::recti bar{bounds.width - width - scale_metric(10, dpi_), scale_metric(6, dpi_),
					                width, bounds.height - scale_metric(12, dpi_)};
					draw.outline(bar, chromeBorder);
					draw.fill({bar.x + 1, bar.y + 1,
					           (std::max)(0, (bar.width - 2) * std::clamp(statusInfo_.progressPercent, 0, 100) / 100),
					           (std::max)(0, bar.height - 2)}, chromeChecked);
				}
				const int sliderWidth = scale_metric(150, dpi_);
				int x = bounds.width - sliderWidth - scale_metric(8, dpi_);
				for (const auto& item : statusToolbar_)
				{
					if (item.kind == ToolbarItemKind::separator)
					{
						x -= scale_metric(8, dpi_);
						continue;
					}
					if (!item.command) continue;
					const int width = command_width(draw, item.command);
					x -= width;
					const app::recti hit{x, 0, width, bounds.height};
					const bool enabled = !item.command->enabled || item.command->enabled();
					const bool checked = item.command->checked && item.command->checked();
					if (checked) draw.fill(hit, chromeChecked);
					draw.text(item.command->toolbarText ? item.command->toolbarText() : item.command->name,
					          hit, enabled ? chromeText : chromeDisabled,
					          TextFormat::center | TextFormat::verticalCenter | TextFormat::singleLine |
					          TextFormat::endEllipsis, font_);
					statusHits_.push_back({hit, item.command, item.menuItems, {}});
				}
				const app::recti slider{bounds.width - sliderWidth, 0, sliderWidth, bounds.height};
				statusHits_.push_back({slider, {}, {}, {}});
			}

			WindowFramePtr owner_;
			WindowFramePtr top_;
			WindowFramePtr statusFrame_;
			FontPtr font_;
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
			int thumbnailSize_{128};
			unsigned int dpi_{96};
			bool taskActive_{};
			bool fullscreen_{};
		};

		MessageResult ChromeReactor::mouse(const WindowFramePtr& frame, const MouseMessage message,
		                                   const MouseInput& input)
		{
			if (const auto owner = owner_.lock()) return owner->mouse(frame, message, input);
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
