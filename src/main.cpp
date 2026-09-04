#include "poplan/console.hpp"
#include "poplan/machine.hpp"

#include <filesystem>
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
        poplan::Machine machine;
        if (argc == 1) {
            return run_default_image(machine, argv[0]);
        }
        if (argc == 2 && std::string(argv[1]) == "--io-demo") {
            return poplan::run_io_shell(machine, std::cin, std::cout);
        }
        if (argc == 3 && std::string(argv[1]) == "--image") {
            return run_image(machine, argv[2]);
        }
        std::cerr << "usage: poplan [--image IMAGE | --io-demo]\n";
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "poplan: " << error.what() << '\n';
        return 1;
    }
}
