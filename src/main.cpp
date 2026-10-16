#include "poplan/console.hpp"
#include "poplan/machine.hpp"
#include "poplan/native.hpp"

#include <filesystem>
#include <charconv>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int run_image(poplan::Machine &machine,
              const std::filesystem::path &image_path)
{
    std::ifstream image(image_path, std::ios::binary);
    if (!image) {
        std::cerr << "poplan: cannot open image: " << image_path << '\n';
        return 2;
    }
    machine.load_image(image);
    return poplan::run_image_shell(machine, std::cin, std::cout);
}

int run_default_image(poplan::Machine &machine, const char *argv0)
{
    const std::filesystem::path executable(argv0);
    const std::vector<std::filesystem::path> candidates = {
        executable.parent_path().parent_path() / "poplan.bin",
        std::filesystem::path("build") / "poplan.bin",
    };
    for (const auto &candidate : candidates) {
        std::ifstream image(candidate, std::ios::binary);
        if (image) {
            machine.load_image(image);
            return poplan::run_image_shell(machine, std::cin, std::cout);
        }
    }
    std::cerr << "poplan: cannot find default image build/poplan.bin; "
                 "run 'make image' or use --image IMAGE\n";
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    try {
        bool native = false, native_controls = false;
        poplan::NativeOptions native_options;
        for (int arg = 1; arg < argc; ++arg) {
            const std::string_view option(argv[arg]);
            if (option == "--native") native = true;
            if (option.substr(0, 9) == "--native-") native_controls = true;
        }
        if (native || native_controls) {
            const auto invalid = [] {
                std::cerr << "usage: poplan --native [--native-closures=historical|experimental] "
                             "[--native-activation-limit=N] [--native-binding-trace]\n";
                return 2;
            };
            if (!native) return invalid();
            bool closures_seen = false, limit_seen = false;
            for (int arg = 1; arg < argc; ++arg) {
                const std::string_view option(argv[arg]);
                if (option == "--native") continue;
                if (option == "--native-binding-trace") {
                    native_options.binding_trace = &std::cerr; continue;
                }
                constexpr std::string_view closure_prefix = "--native-closures=";
                constexpr std::string_view limit_prefix = "--native-activation-limit=";
                if (option.substr(0, closure_prefix.size()) == closure_prefix && !closures_seen) {
                    closures_seen = true;
                    const auto value = option.substr(closure_prefix.size());
                    if (value != "historical" && value != "experimental") return invalid();
                    native_options.experimental_closures = value == "experimental";
                    continue;
                }
                if (option.substr(0, limit_prefix.size()) == limit_prefix && !limit_seen) {
                    limit_seen = true;
                    const auto value = option.substr(limit_prefix.size());
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                                        native_options.activation_limit);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                        || !native_options.activation_limit) return invalid();
                    continue;
                }
                return invalid();
            }
            return poplan::run_native_shell(std::cin, std::cout, std::cerr, native_options);
        }
        if (argc == 2 && std::string(argv[1]) == "--io-demo") {
            poplan::Machine machine;
            return poplan::run_io_shell(machine, std::cin, std::cout);
        }
        struct Mount { unsigned unit; std::string path; bool writable; };
        std::vector<Mount> mounts;
        std::string image_path;
        const auto usage = [] {
            std::cerr << "usage: poplan [--image IMAGE] [--disk UNIT=PATH | --disk-rw UNIT=PATH]...\n"
                         "       poplan --io-demo | --native\n";
            return 2;
        };
        for (int arg = 1; arg < argc; ++arg) {
            const std::string option = argv[arg];
            if (arg + 1 >= argc) return usage();
            const std::string value = argv[++arg];
            if (option == "--image" && image_path.empty()) { image_path = value; continue; }
            if (option != "--disk" && option != "--disk-rw") return usage();
            const auto equals = value.find('=');
            if (equals == std::string::npos || equals == 0 || equals + 1 == value.size()) return usage();
            unsigned unit = 0;
            for (std::size_t i = 0; i < equals; ++i) {
                if (value[i] < '0' || value[i] > '7' || unit > 7) return usage();
                unit = unit * 8 + value[i] - '0';
            }
            for (const auto &mount : mounts) if (mount.unit == unit) return usage();
            mounts.push_back({unit, value.substr(equals + 1), option == "--disk-rw"});
        }
        poplan::Machine machine;
        for (const auto &mount : mounts) machine.mount_disk(mount.unit, mount.path, mount.writable);
        return image_path.empty() ? run_default_image(machine, argv[0]) : run_image(machine, image_path);
    } catch (const std::exception &error) {
        std::cerr << "poplan: " << error.what() << '\n';
        return 1;
    }
}
