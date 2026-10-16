#include "pop/runtime.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
namespace {
constexpr const char *usage =
    "Usage: poplan-native [--library-path DIR] [--max-frames N]\n"
    "                     [--max-operations N] [--stats] [FILE...]\n"
    "Reads POPLAN source from FILEs, or standard input when none are given.\n";
std::size_t positive(const std::string &text) {
  std::size_t value = 0;
  auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      !value)
    throw pop::Fault("positive integer required for limit");
  return value;
}
} // namespace
int main(int argc, char **argv) {
  try {
    pop::Options options;
    std::vector<std::string> files;
    bool stats = false;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << usage;
        return 0;
      }
      if (arg == "--stats")
        stats = true;
      else if (arg == "--library-path" && i + 1 < argc)
        options.library_paths.push_back(argv[++i]);
      else if (arg == "--max-frames" && i + 1 < argc)
        options.max_frames = positive(argv[++i]);
      else if (arg == "--max-operations" && i + 1 < argc)
        options.max_operations = positive(argv[++i]);
      else if (!arg.empty() && arg[0] == '-')
        throw pop::Fault(usage);
      else
        files.push_back(arg);
    }
    options.library_paths.push_back(
        (std::filesystem::absolute(argv[0]).parent_path() / "lib").string());
    options.library_paths.push_back(
        (std::filesystem::absolute(argv[0]).parent_path() /
         "../share/poplan/lib")
            .string());
    pop::Runtime runtime(std::cin, std::cout, std::cerr, options);
    auto submit = [&](std::istream &stream, const std::string &name,
                      bool terminal) {
      std::string line;
      while (true) {
        if (terminal)
          std::cout << ": " << std::flush;
        if (!std::getline(stream, line))
          break;
        auto result = runtime.feed(name, line + "\n", false);
        std::cout.flush();
        if (result.state == pop::Runtime::SessionState::failed &&
            options.max_operations &&
            runtime.operations >= options.max_operations)
          return;
      }
      runtime.feed(name, "", true);
    };
    if (files.empty())
      submit(std::cin, "<stdin>", isatty(STDIN_FILENO) != 0);
    else
      for (auto &path : files) {
        std::ifstream file(path);
        if (!file)
          throw pop::Fault("cannot open " + path);
        submit(file, path, false);
      }
    if (stats)
      std::cerr << "Native operations: " << runtime.operations
                << "; maximum frame depth: " << runtime.max_depth << '\n';
    return runtime.diagnostics ? 1 : 0;
  } catch (const std::exception &error) {
    std::cerr << "poplan-native: " << error.what() << '\n';
    return 1;
  }
}
