// Fuzz Mir's static display configuration reader, miral::YamlFileDisplayConfig (the parser behind
// miral::DisplayConfiguration and the "<shell>.display" YAML file a user edits):
// src/miral/static_display_config.cpp load_config -> layouts/cards/outputs parsing, then for every
// layout select_layout + apply_to on a two-output display configuration (VGA + HDMI with an EDID),
// and serialize_configuration of the result.
//
// load_config reports every problem in the file by throwing mir::AbnormalExit (yaml-cpp errors are
// rethrown as AbnormalExit too); the real caller logs it, so the harness catches exactly that.
// A fresh config object is built per input; the Mir logger is replaced once by a sink that drops
// messages.
#include "static_display_config.h"

#include <mir/abnormal_exit.h>
#include <mir/logging/logger.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace mg = mir::graphics;

namespace
{
struct NullLogger : mir::logging::Logger
{
    void log(mir::logging::Severity, std::string const&, std::string const&) override {}
};

std::vector<std::uint8_t> const edid{
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x59, 0x96, 0x01, 0x30, 0x01, 0x00, 0x00, 0x00,
    0x1e, 0x1c, 0x01, 0x04, 0xa5, 0x0a, 0x0f, 0x78, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x0f, 0x1b, 0x20, 0x48, 0x30, 0x00, 0x2c, 0x50, 0x20, 0x14,
    0x02, 0x04, 0x3c, 0x3c, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x41, 0x4e, 0x58,
    0x37, 0x35, 0x33, 0x30, 0x20, 0x55, 0x0a, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x93,
};

mg::DisplayConfigurationOutput make_output(int id, mg::DisplayConfigurationOutputType type, char const* name)
{
    mg::DisplayConfigurationOutput out;
    out.id = mg::DisplayConfigurationOutputId{id};
    out.card_id = mg::DisplayConfigurationCardId{0};
    out.connected = true;
    out.type = type;
    out.used = true;
    out.top_left = {0, 0};
    out.preferred_mode_index = 0;
    out.current_mode_index = 0;
    out.modes.push_back({{648, 480}, 60.0});
    out.modes.push_back({{1280, 1024}, 75.0});
    out.power_mode = mir_power_mode_on;
    out.orientation = mir_orientation_normal;
    out.name = name;
    return out;
}

struct TwoOutputConfig : mg::DisplayConfiguration
{
    std::vector<mg::DisplayConfigurationOutput> outputs;

    TwoOutputConfig()
    {
        outputs.push_back(make_output(0, mg::DisplayConfigurationOutputType::vga, "VGA-1"));
        outputs.push_back(make_output(1, mg::DisplayConfigurationOutputType::hdmia, "HDMI-A-1"));
        outputs.back().display_info = mg::DisplayInfo{edid};
    }

    void for_each_output(std::function<void(mg::DisplayConfigurationOutput const&)> f) const override
    {
        for (auto const& o : outputs) f(o);
    }
    void for_each_output(std::function<void(mg::UserDisplayConfigurationOutput&)> f) override
    {
        for (auto& o : outputs)
        {
            mg::UserDisplayConfigurationOutput u{o};
            f(u);
        }
    }
    std::unique_ptr<mg::DisplayConfiguration> clone() const override
    {
        auto copy = std::make_unique<TwoOutputConfig>();
        copy->outputs = outputs;
        return copy;
    }
};
}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    mir::logging::set_logger(std::make_shared<NullLogger>());
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
    miral::YamlFileDisplayConfig cfg;
    cfg.add_output_attribute("custom");
    cfg.layout_userdata_builder("extra", [](YAML::Node const&) { return std::any{1}; });

    std::istringstream in{std::string{reinterpret_cast<char const*>(data), size}};
    try
    {
        cfg.load_config(in, "fuzz.display");
    }
    catch (mir::AbnormalExit const&)
    {
        return 0;
    }

    for (auto const& layout : cfg.list_layouts())
    {
        cfg.select_layout(layout);
        TwoOutputConfig conf;
        cfg.apply_to(conf);
        (void)cfg.layout_userdata("extra");
        std::ostringstream out;
        miral::YamlFileDisplayConfig::serialize_configuration(out, conf);
    }
    return 0;
}
