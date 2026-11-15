#include "libdesktop/ui.h"

namespace
{
    // The window's content: what it lays out and paints.
    class app_view final : public ui::frame_host
    {
        core::sizei _extent;

    public:
        void on_window_layout(render::measure_context&, const core::sizei extent, bool) override
        {
            _extent = extent;
        }

        void on_window_paint(render::draw_context& dc) override
        {
            const core::recti bounds(_extent);
            dc.draw_rect(bounds, core::color(core::rgba(245, 247, 249)));
            const auto inset = core::round(24 * dc.scale_factor);
            dc.draw_text("@NAME@", bounds.inflate(-inset), render::font_face::title, render::text_style::single_line,
                         core::color(core::rgba(25, 35, 45)), {});
        }
    };

    class app final : public ui::app, public std::enable_shared_from_this<app>
    {
        platform::setting_file_ptr _settings;
        std::shared_ptr<app_view> _view = std::make_shared<app_view>();
        ui::frame_ptr _frame;

    public:
        bool load_settings(const platform::setting_file_ptr& store) override
        {
            _settings = store;
            return true;
        }

        bool init(std::string_view) override
        {
            ui::frame_style style;
            style.drawn_caption = false;
            style.client_size = {960, 640};
            _frame = ui::create_app_frame(shared_from_this(), _settings, _view, style);
            if (!_frame) return false;
            _frame->window_text("@NAME@");
            return true;
        }

        void final_exit() override
        {
            _frame.reset();
        }

        bool wants_gpu() const override { return false; }
        bool key_down(char32_t, platform::key_state) override { return false; }
        bool text_input(std::string_view) override { return false; }
        ui::focus_mode focus_mode() const override { return ui::focus_mode::none; }

        void track_menu(const ui::frame_ptr& parent, const core::recti bounds,
                        const std::vector<ui::command_ptr>& commands) override
        {
            if (parent) parent->track_menu(bounds, commands);
        }
    };
}

int main(int argc, char** argv)
{
    core::set_app_name("@NAME@");
    return platform::run(std::make_shared<app>(), argc, argv);
}
