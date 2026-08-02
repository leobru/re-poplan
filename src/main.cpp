#include "poplan/console.hpp"
#include "poplan/machine.hpp"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    try {
        poplan::Machine machine;
        if (argc == 1) {
            return poplan::run_io_shell(machine, std::cin, std::cout);
        }
        if (argc == 3 && std::string(argv[1]) == "--image") {
            std::ifstream image(argv[2], std::ios::binary);
            if (!image) {
                std::cerr << "poplan: cannot open image: " << argv[2]
                          << '\n';
                return 2;
            }
            machine.load_image(image);
            return poplan::run_image_shell(
                machine, std::cin, std::cout);
        }
        std::cerr << "usage: poplan [--image IMAGE]\n";
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "poplan: " << error.what() << '\n';
        return 1;
    }
}
