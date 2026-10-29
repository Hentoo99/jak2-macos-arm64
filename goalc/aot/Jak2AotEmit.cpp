/*!
 * jak2-aot-emit: emit signed-module assembly for ARM64 GOAL objects.
 *
 *   jak2-aot-emit --obj-dir out/jak2-arm64/obj --modules gcommon,gkernel
 *                 --function-type 0x... --output-dir DIR
 *                 [--data-base 0xa000000] [--data-limit 0xe000000]
 *                 [--arena-size 0x10000000] [--omit-debug]
 *
 * Writes DIR/modules/<symbol>.S, DIR/arena-layout.S, DIR/module-index.c and
 * DIR/manifest.json.  Link arena-layout before the modules so the __ARENA
 * segment precedes __GOAL.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "goalc/aot/SignedModuleEmitter.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  fs::path obj_dir;
  fs::path output_dir;
  std::vector<std::string> modules;
  goal_aot::SignedModuleOptions module;
  goal_aot::ArenaOptions arena{0x10000000, 0xa000000, 0xe000000, 256};
};

[[noreturn]] void usage_error(const std::string& message) {
  throw std::invalid_argument(
      message +
      "\nusage: jak2-aot-emit --obj-dir DIR --modules A,B,... | --module-list FILE "
      "--function-type HEX --output-dir DIR [--data-base HEX] [--data-limit HEX] "
      "[--arena-size HEX] [--omit-debug]");
}

uint32_t parse_u32(const std::string& text, const char* name) {
  size_t used = 0;
  unsigned long long value = 0;
  try {
    value = std::stoull(text, &used, 0);
  } catch (const std::exception&) {
    used = 0;
  }
  if (used != text.size() || value > 0xFFFFFFFFull) {
    usage_error(std::string("invalid value for ") + name + ": " + text);
  }
  return (uint32_t)value;
}

void add_modules(std::vector<std::string>& out, const std::string& list, char separator) {
  std::stringstream ss(list);
  std::string item;
  while (std::getline(ss, item, separator)) {
    auto begin = item.find_first_not_of(" \t\r");
    auto end = item.find_last_not_of(" \t\r");
    if (begin != std::string::npos && item[begin] != '#') {
      out.push_back(item.substr(begin, end - begin + 1));
    }
  }
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    auto value = [&](const char* name) {
      if (i + 1 >= argc) {
        usage_error(std::string("missing value for ") + name);
      }
      return std::string(argv[++i]);
    };
    if (argument == "--obj-dir") {
      options.obj_dir = value("--obj-dir");
    } else if (argument == "--output-dir") {
      options.output_dir = value("--output-dir");
    } else if (argument == "--modules") {
      add_modules(options.modules, value("--modules"), ',');
    } else if (argument == "--module-list") {
      std::ifstream in(value("--module-list"));
      if (!in) {
        usage_error("cannot read module list");
      }
      std::stringstream ss;
      ss << in.rdbuf();
      add_modules(options.modules, ss.str(), '\n');
    } else if (argument == "--function-type") {
      options.module.function_type = parse_u32(value("--function-type"), "--function-type");
    } else if (argument == "--data-base") {
      options.arena.data_base = parse_u32(value("--data-base"), "--data-base");
    } else if (argument == "--data-limit") {
      options.arena.data_limit = parse_u32(value("--data-limit"), "--data-limit");
    } else if (argument == "--arena-size") {
      options.arena.arena_size = parse_u32(value("--arena-size"), "--arena-size");
    } else if (argument == "--omit-debug") {
      options.module.omit_debug_segment = true;
    } else if (argument == "--help") {
      usage_error("");
    } else {
      usage_error("unknown argument: " + argument);
    }
  }
  if (options.obj_dir.empty() || options.output_dir.empty() || options.modules.empty() ||
      !options.module.function_type) {
    usage_error("--obj-dir, --output-dir, modules and --function-type are required");
  }
  return options;
}

std::vector<uint8_t> read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, const std::string& contents) {
  std::ofstream out(path, std::ios::binary);
  out << contents;
  if (!out) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    std::vector<goal_aot::SignedModule> modules;
    for (const auto& name : options.modules) {
      const auto bytes = read_file(options.obj_dir / (name + ".o"));
      modules.push_back(goal_aot::emit_signed_module(name, bytes, options.module));
    }
    const auto layout = goal_aot::emit_arena_layout(modules, options.arena);

    fs::create_directories(options.output_dir / "modules");
    nlohmann::json manifest;
    manifest["function_type"] = options.module.function_type;
    manifest["omit_debug_segment"] = options.module.omit_debug_segment;
    manifest["arena_size"] = options.arena.arena_size;
    manifest["data_base"] = options.arena.data_base;
    manifest["data_end"] = layout.data_end;
    manifest["data_offsets"] = layout.manifest;
    manifest["modules"] = nlohmann::json::array();
    uint64_t code_bytes = 0, static_bytes = 0;
    for (const auto& mod : modules) {
      write_file(options.output_dir / "modules" / (mod.symbol_base + ".S"), mod.assembly);
      manifest["modules"].push_back(mod.manifest);
      for (const auto& seg : mod.segments) {
        code_bytes += seg.code_size;
        static_bytes += seg.static_size;
      }
    }
    write_file(options.output_dir / "arena-layout.S", layout.assembly);
    write_file(options.output_dir / "module-index.c",
               goal_aot::emit_module_index(modules, options.module.function_type));
    write_file(options.output_dir / "manifest.json", manifest.dump(1) + "\n");
    std::cout << "jak2-aot-emit: " << modules.size() << " modules, " << code_bytes
              << " bytes of code, " << static_bytes << " bytes of static data\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "jak2-aot-emit: " << e.what() << "\n";
    return 1;
  }
}
