// ImageWalker by Zac Walker
// Declares the mode strip down the left edge and the folder shortcuts beneath it.

#pragma once

#include "CanvasRenderer.h"
#include "Dpi.h"
#include "Platform.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iw
{
	// 2.0's way into a mode, drawn rather than hosted: one column of 64px buttons with a glyph over
	// a caption, and the user's folders underneath. It posts the command and the frame decides, so
	// a refusal cannot leave the bar showing a mode the frame is not in.
	class ModeBar final : public platform::FrameReactor, public std::enable_shared_from_this<ModeBar>
	{
	public:
		enum class Glyph { browse, folders, convert, rename, edit, sync, fullscreen, folder, pin };

		struct Button
		{
			int id{};
			Glyph glyph{Glyph::browse};
			std::wstring caption;
			std::filesystem::path path; // shortcuts only
			bool checked{};
			bool enabled{true};
			bool removable{};
		};

		bool create(const platform::WindowFramePtr& parent);
		platform::WindowFramePtr frame() const { return frame_; }

		void set_modes(std::vector<Button> modes);
		void set_shortcuts(std::vector<Button> shortcuts);
		void set_checked(int id);
		void set_enabled(int id, bool enabled);
		void set_dpi(unsigned int dpi);
		int width() const { return metric(buttonEdge) + metric(4); }

		void set_command_handler(std::function<void(const Button&)> handler)
		{
			commandHandler_ = std::move(handler);
		}

		void set_remove_handler(std::function<void(const Button&)> handler)
		{
			removeHandler_ = std::move(handler);
		}

	private:
		static constexpr int buttonEdge = 64;

		platform::MessageResult mouse(const platform::WindowFramePtr& frame, platform::MouseMessage message,
		                              const platform::MouseInput& input) override;
		void paint(const platform::WindowFramePtr& frame, platform::DrawContext& draw) override;
		void size(const platform::WindowFramePtr& frame, sizei extent,
		          platform::MeasureContext& measure) override;

		int metric(const int value) const { return ui::scale_metric(value, dpi_); }
		void layout();
		void draw_button(const ui::CanvasRenderer& renderer, const Button& button, recti bounds,
		                 bool hot) const;
		void draw_glyph(const ui::CanvasRenderer& renderer, Glyph glyph, recti bounds, color value) const;
		const Button* button_at(pointi point) const;
		void invalidate();

		platform::WindowFramePtr frame_;
		std::vector<Button> modes_;
		std::vector<Button> shortcuts_;
		std::vector<recti> modeBounds_;
		std::vector<recti> shortcutBounds_;
		std::function<void(const Button&)> commandHandler_;
		std::function<void(const Button&)> removeHandler_;
		unsigned int dpi_{96};
		int hotId_{};
		int separatorY_{};
		bool trackingMouse_{};
	};

	using ModeBarPtr = std::shared_ptr<ModeBar>;
}
