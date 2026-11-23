// Fuzz Mir's live-config INI reader: miral::live_config::IniFile::load_file, the public API a
// Mir shell uses to read its user-editable "<shell>.conf" file (src/miral/live_config_ini_file*.cpp,
// basic_store.cpp key/value parsing, live_config.cpp key validation).
//
// One attribute of every kind is registered, with and without presets, like a real shell does.
// A fresh IniFile is built per input. The only process-global state is the Mir logger, replaced
// once at start-up by a sink that drops messages (read-only after that).
#include <miral/live_config_ini_file.h>
#include <mir/logging/logger.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace mlc = miral::live_config;

namespace
{
struct NullLogger : mir::logging::Logger
{
    void log(mir::logging::Severity, std::string const&, std::string const&) override {}
};

volatile std::size_t sink;
}

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    mir::logging::set_logger(std::make_shared<NullLogger>());
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
    mlc::IniFile ini;
    auto touch = [](auto const& key) { sink = sink + key.to_string().size(); };

    ini.add_int_attribute({"scope", "int"}, "an int",
        [&](mlc::Key const& k, std::optional<int> v) { touch(k); if (v) sink = sink + *v; });
    ini.add_int_attribute({"scope", "int_preset"}, "an int", 7,
        [&](mlc::Key const& k, std::optional<int> v) { touch(k); if (v) sink = sink + *v; });
    ini.add_ints_attribute({"ints"}, "ints",
        [&](mlc::Key const& k, std::optional<std::span<int const>> v) { touch(k); if (v) for (int i : *v) sink = sink + i; });
    int const ints_preset[] = {1, 2, 3};
    ini.add_ints_attribute({"scope", "ints_preset"}, "ints", ints_preset,
        [&](mlc::Key const& k, std::optional<std::span<int const>> v) { touch(k); if (v) for (int i : *v) sink = sink + i; });
    ini.add_bool_attribute({"scope", "bool"}, "a bool",
        [&](mlc::Key const& k, std::optional<bool> v) { touch(k); if (v) sink = sink + *v; });
    ini.add_bool_attribute({"bool_preset"}, "a bool", true,
        [&](mlc::Key const& k, std::optional<bool> v) { touch(k); if (v) sink = sink + *v; });
    ini.add_float_attribute({"float"}, "a float",
        [&](mlc::Key const& k, std::optional<float> v) { touch(k); if (v) sink = sink + static_cast<std::size_t>(*v != 0); });
    ini.add_float_attribute({"scope", "float_preset"}, "a float", 1.5f,
        [&](mlc::Key const& k, std::optional<float> v) { touch(k); if (v) sink = sink + static_cast<std::size_t>(*v != 0); });
    ini.add_floats_attribute({"floats"}, "floats",
        [&](mlc::Key const& k, std::optional<std::span<float const>> v) { touch(k); if (v) sink = sink + v->size(); });
    float const floats_preset[] = {0.5f, 2.0f};
    ini.add_floats_attribute({"floats_preset"}, "floats", floats_preset,
        [&](mlc::Key const& k, std::optional<std::span<float const>> v) { touch(k); if (v) sink = sink + v->size(); });
    ini.add_string_attribute({"scope", "string"}, "a string",
        [&](mlc::Key const& k, std::optional<std::string_view> v) { touch(k); if (v) sink = sink + v->size(); });
    ini.add_string_attribute({"string_preset"}, "a string", "preset",
        [&](mlc::Key const& k, std::optional<std::string_view> v) { touch(k); if (v) sink = sink + v->size(); });
    ini.add_strings_attribute({"strings"}, "strings",
        [&](mlc::Key const& k, std::optional<std::span<std::string const>> v) { touch(k); if (v) for (auto const& s : *v) sink = sink + s.size(); });
    std::string const strings_preset[] = {"a", "b"};
    ini.add_strings_attribute({"scope", "strings_preset"}, "strings", strings_preset,
        [&](mlc::Key const& k, std::optional<std::span<std::string const>> v) { touch(k); if (v) for (auto const& s : *v) sink = sink + s.size(); });
    ini.on_done([] { sink = sink + 1; });

    std::istringstream in{std::string{reinterpret_cast<char const*>(data), size}};
    ini.load_file(in, "fuzz.conf");
    return 0;
}
