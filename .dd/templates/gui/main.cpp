#include "platform.h"

namespace
{
    pf::window_frame_ptr main_frame;
}

class app_view final : public pf::frame_reactor
{
public:
    uint32_t handle_message(pf::window_frame_ptr, pf::message_type, const pf::message_params&) override { return 0; }
    void handle_size(pf::window_frame_ptr&, pf::isize, pf::measure_context&) override {}
    void handle_paint(pf::window_frame_ptr& frame, pf::draw_context& draw) override
    {
        const auto bounds = frame->get_client_rect();
        draw.fill_solid_rect(bounds, {245, 247, 249});
        draw.draw_text(24, 24, {24, 24, bounds.right - 24, 64}, "@NAME@",
            {20, pf::font_name::calibri}, {25, 35, 45}, {245, 247, 249});
    }
};

app_init_result app_init(const pf::window_frame_ptr& frame, std::span<const std::string_view>)
{
    main_frame = frame;
    frame->set_reactor(std::make_shared<app_view>());
    return {};
}

void app_idle()
{
    if (main_frame)
    {
        main_frame->set_text("@NAME@");
        main_frame.reset();
    }
}

void app_destroy() { main_frame.reset(); }