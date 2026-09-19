#include <borealis/disc.hpp>

#include <xxhash.h>

#include <array>
#include <cstdio>
#include <string>

namespace borealis::disc {
namespace {

constexpr uint32_t kContainerGcMagic = 0xC2339F3Du;
constexpr long kMinDiscSize = 16L * 1024 * 1024;

uint32_t read_be32(const unsigned char* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint32_t container_magic(const unsigned char* hdr) {
  return static_cast<uint32_t>(hdr[0]) | (static_cast<uint32_t>(hdr[1]) << 8) |
         (static_cast<uint32_t>(hdr[2]) << 16) |
         (static_cast<uint32_t>(hdr[3]) << 24);
}

bool is_container(uint32_t magic) {
  switch (magic) {
  case 0xB10BC001u:
  case 0x4F534943u:
  case 0xA2380FAEu:
  case 0x53464257u:
  case 0x01414957u:
  case 0x015A5652u:
  case 0x53474745u:
    return true;
  default:
    return false;
  }
}

struct Header {
  bool ok = false;
  bool container = false;
  std::string game_id;
  uint8_t disc_number = 0;
  uint8_t revision = 0;
  uint64_t size = 0;
};

Header read_header(const std::string& location) {
  Header header;
  FILE* file = std::fopen(location.c_str(), "rb");
  if (file == nullptr)
    return header;
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return header;
  }
  const long size = std::ftell(file);
  if (size < kMinDiscSize) {
    std::fclose(file);
    return header;
  }
  header.size = static_cast<uint64_t>(size);
  std::array<unsigned char, 0x20> hdr{};
  std::fseek(file, 0, SEEK_SET);
  if (std::fread(hdr.data(), 1, hdr.size(), file) != hdr.size()) {
    std::fclose(file);
    return header;
  }
  std::fclose(file);
  if (is_container(container_magic(hdr.data()))) {
    header.ok = true;
    header.container = true;
    return header;
  }
  if (read_be32(hdr.data() + 0x1C) != kContainerGcMagic)
    return header;
  header.ok = true;
  header.game_id = std::string(
      reinterpret_cast<const char*>(hdr.data()), 6);
  header.disc_number = hdr[6];
  header.revision = hdr[7];
  return header;
}

std::optional<std::size_t> match_catalog(
    Catalog catalog, const std::string& game_id) {
  for (std::size_t i = 0; i < catalog.acceptedDiscs.size(); ++i) {
    if (catalog.acceptedDiscs[i].gameId == game_id)
      return i;
  }
  return std::nullopt;
}

}  // namespace

Result inspect(std::string_view location, Catalog catalog) {
  Result result;
  const Header header = read_header(std::string(location));
  if (!header.ok) {
    FILE* probe = std::fopen(std::string(location).c_str(), "rb");
    if (probe == nullptr) {
      result.status = Status::IOError;
      result.message = "cannot open file";
    } else {
      std::fclose(probe);
      result.status = Status::InvalidImage;
      result.message = "not a GameCube disc image";
    }
    return result;
  }
  if (header.container) {
    result.status = Status::UnsupportedVersion;
    result.message = "compressed container needs nod support";
    return result;
  }
  result.metadata.gameId = header.game_id;
  result.metadata.platform = Platform::GameCube;
  result.metadata.discNumber = header.disc_number;
  result.metadata.revision = header.revision;
  result.metadata.logicalSize = header.size;
  result.acceptedDiscIndex = match_catalog(catalog, header.game_id);
  if (!result.acceptedDiscIndex) {
    bool recognized = false;
    for (const std::string_view id : catalog.recognizedGameIds) {
      if (id == header.game_id) {
        recognized = true;
        break;
      }
    }
    result.status =
        recognized ? Status::UnsupportedVersion : Status::UnknownGame;
    result.message = "game ID " + header.game_id;
    return result;
  }
  result.status = Status::Success;
  return result;
}

Result verify(std::string_view location, Catalog catalog, Progress* progress) {
  Result result = inspect(location, catalog);
  if (result.status != Status::Success)
    return result;
  if (!result.acceptedDiscIndex) {
    result.status = Status::UnknownGame;
    return result;
  }
  const std::string path(location);
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    result.status = Status::IOError;
    return result;
  }
  if (progress != nullptr)
    progress->bytesTotal.store(result.metadata.logicalSize);
  XXH3_state_t* state = XXH3_createState();
  XXH3_128bits_reset(state);
  std::array<unsigned char, 1 << 20> chunk{};
  uint64_t total = 0;
  size_t got = 0;
  while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) > 0) {
    XXH3_128bits_update(state, chunk.data(), got);
    total += got;
    if (progress != nullptr) {
      progress->bytesRead.store(total);
      if (progress->cancelRequested.load()) {
        XXH3_freeState(state);
        std::fclose(file);
        result.status = Status::Canceled;
        return result;
      }
    }
  }
  const XXH128_hash_t hash = XXH3_128bits_digest(state);
  XXH3_freeState(state);
  std::fclose(file);
  const XXH128_hash_t expected =
      catalog.acceptedDiscs[*result.acceptedDiscIndex].expectedHash;
  if (hash.low64 != expected.low64 || hash.high64 != expected.high64) {
    result.status = Status::HashMismatch;
    result.message = "XXH3-128 mismatch";
    return result;
  }
  result.status = Status::Success;
  return result;
}

}  // namespace borealis::disc
