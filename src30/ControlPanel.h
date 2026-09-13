// ImageWalker by Zac Walker
// Declares the drawn control set used by the task-view controls panel.

#pragma once

#include "CanvasRenderer.h"
#include "Dpi.h"
#include "Platform.h"
#include "util_layout.h"

#include <functional>
#include <string>
#include <vector>

namespace iw::ui
{
	enum class ControlKind
	{
		heading,
		label,
		checkBox,
		radio,
		slider,
		choice,
		folder,
		text,
		button,
		link,
		separator
	};

	struct Control
	{
		int id{};
		ControlKind kind{ControlKind::label};
		std::wstring label;
		std::wstring text; // folder path, chosen choice, or edited text
		std::wstring suffix; // slider value readout unit
		bool checked{};
		int value{};
		int minimum{};
		int maximum{100};
		int group{}; // radio exclusivity, and nothing else
		int indent{};
		bool enabled{true};
		std::vector<std::wstring> choices;
		std::function<void()> changed;

		recti bounds; // whole row, filled by layout
		recti hit; // the part that responds to the mouse
		std::vector<std::wstring> wrapped; // label lines, filled by layout
	};

	// A vertical stack of drawn controls. It owns hit testing and value changes; anything that
	// needs a window - a folder picker, a popup menu, a hosted edit - is raised to the host.
	class ControlPanel
	{
	public:
		void set_dpi(const unsigned int dpi) { dpi_ = dpi ? dpi : 96; }
		unsigned int dpi() const { return dpi_; }
		void clear()
		{
			controls_.clear();
			contentHeight_ = hotId_ = pressedId_ = focusedId_ = 0;
			dragging_ = false;
		}
		Control& add(Control control);
		Control* find(int id);
		const Control* find(int id) const;
		std::vector<Control>& controls() { return controls_; }
		const std::vector<Control>& controls() const { return controls_; }

		void layout(recti bounds, const CanvasRenderer& renderer);
		void draw(const CanvasRenderer& renderer) const;
		int content_height() const { return contentHeight_; }

		// The host opens pickers, popups and links; the panel never creates a window.
		void set_activate_handler(std::function<void(Control&)> handler) { activate_ = std::move(handler); }
		void set_invalidate_handler(std::function<void()> handler) { invalidate_ = std::move(handler); }

		bool mouse_move(pointi point);
		bool mouse_down(pointi point);
		bool mouse_up(pointi point);
		void mouse_leave();
		bool dragging() const { return dragging_; }
		int hot_id() const { return hotId_; }
		int focused_id() const { return focusedId_; }
		bool focus(int id);
		bool focus_next(bool reverse = false);
		bool key(const platform::KeyInput& input);
		void set_interactive(bool enabled);
		bool interactive() const { return interactive_; }

		// The row a hosted edit control must occupy, or an empty rect when there is none.
		recti text_bounds(int id) const;

		void set_checked(int id, bool checked);
		void set_value(int id, int value);
		void set_enabled(int id, bool enabled);
		void set_text(int id, std::wstring text);

	private:
		int metric(const int value) const { return scale_metric(value, dpi_); }
		Control* control_at(pointi point);
		void apply_radio(const Control& chosen);
		int slider_value_at(const Control& control, int x) const;
		static bool focusable(const Control& control);
		void notify_changed(const Control& control) const;

		std::vector<Control> controls_;
		std::function<void(Control&)> activate_;
		std::function<void()> invalidate_;
		unsigned int dpi_{96};
		int contentHeight_{};
		int hotId_{};
		int pressedId_{};
		int focusedId_{};
		bool dragging_{};
		bool interactive_{true};
	};
}
