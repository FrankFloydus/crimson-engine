////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_debug_dump.hpp"
#include "io/qdr/qdr_scene_binary_codec.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("Could not open QDR scene: " + path.string());
  }
  const std::string bytes{std::istreambuf_iterator<char>(stream),
                          std::istreambuf_iterator<char>()};
  if (stream.bad()) {
    throw std::runtime_error("Could not read QDR scene: " + path.string());
  }
  const auto *begin = reinterpret_cast<const std::byte *>(bytes.data());
  return {begin, begin + bytes.size()};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3 || std::string(argv[1]) != "--json") {
    std::cerr << "Usage: qdr_dump --json <scene.qdr>\n";
    return 2;
  }

  try {
    quader_io::QdrBinaryContainerReadResult container =
        quader_io::read_qdr_binary_container(
            read_bytes(std::filesystem::path(argv[2])));
    if (!container.ok) {
      std::cerr << (container.message.empty()
                        ? "QDR binary container is invalid."
                        : container.message)
                << '\n';
      return 1;
    }
    quader::SceneContentDocument document =
        quader::decode_qdr_scene_binary(container);
    std::cout << quader::qdr_scene_debug_json(document, container) << '\n';
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
