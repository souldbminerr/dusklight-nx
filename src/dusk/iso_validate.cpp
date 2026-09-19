#include "iso_validate.hpp"

#include <borealis/disc.hpp>

#include <array>
#include <atomic>
#include <string_view>

#ifdef __SWITCH__
#include <switch/switch_disc_image.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#endif

#include "dusk/logging.h"
#include "dusk/settings.h"

namespace {

const char* verification_state_name(dusk::DiscVerificationState state) noexcept {
    switch (state) {
    case dusk::DiscVerificationState::Success:
        return "verified";
    case dusk::DiscVerificationState::HashMismatch:
        return "hash mismatch";
    case dusk::DiscVerificationState::Unknown:
    default:
        return "unknown";
    }
}

}  // namespace

namespace dusk::iso {
namespace {



constexpr auto AcceptedDiscs = std::to_array<borealis::disc::AcceptedDisc>({
    {
        .gameId = "GZ2E01",
        .expectedHash = borealis::disc::parse_xxh3_128("14e886f08e548a000afde98a3195e788"),
    },
    {
        .gameId = "GZ2J01",
        .expectedHash = borealis::disc::parse_xxh3_128("5967dc7a6a553652f4d2050aeef6f368"),
    },
    {
        .gameId = "GZ2P01",
        .expectedHash = borealis::disc::parse_xxh3_128("9ef597588b0035ca9e91b333fa9a8a7e"),
    },
    {
        .gameId = "RZDE01",
        .revision = 0,
        .expectedHash = borealis::disc::parse_xxh3_128("b3d91fbea59e5c66934d04c01566728e"),
    },
    {
        .gameId = "RZDE01",
        .revision = 2,
        .expectedHash = borealis::disc::parse_xxh3_128("c3ec420921a1b36d6ae43f576491d25c"),
    },
    {
        .gameId = "RZDJ01",
        .expectedHash = borealis::disc::parse_xxh3_128("d3866821c7fc6999e6e8bbef8b6875aa"),
    },
    {
        .gameId = "RZDP01",
        .expectedHash = borealis::disc::parse_xxh3_128("6095a924a57e5fb4294ac96fb85a09a1"),
    },
});

constexpr auto RecognizedGameIds = std::to_array<std::string_view>({"RZDK01"});

Region region_from_game_id(std::string_view gameId) noexcept {
    if (gameId.size() >= 4) {
        switch (gameId[3]) {
        case 'P':
            return Region::Europe;
        case 'J':
            return Region::Japan;
        case 'K':
            return Region::Korea;
        default:
            break;
        }
    }
    return Region::NorthAmerica;
}

#ifdef __SWITCH__
// Switch has no nod/Rust: validate through the platform disc image readers
// (raw ISO plus GCZ/TGC/CISO/WBFS/WIA/RVZ) in logical disc coordinates.
constexpr std::uint64_t kMinDiscSize = 16ULL * 1024 * 1024;
constexpr std::uint32_t kGcMagic = 0xC2339F3Du;
constexpr std::uint32_t kWiiMagic = 0x5D1C9EA3u;

struct SwitchDiscHeader {
  bool ok = false;
  bool io_error = false;
  Platform platform = Platform::Unknown;
  std::string game_id;
  std::uint8_t disc_number = 0;
  std::uint8_t revision = 0;
};

std::uint32_t read_be32(const unsigned char* data) noexcept {
  return (static_cast<std::uint32_t>(data[0]) << 24) |
         (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

SwitchDiscHeader read_switch_header(const char* path) {
  SwitchDiscHeader header;
  if (path == nullptr || path[0] == 0) {
    header.io_error = true;
    return header;
  }
  auto opened = dusk::sw::disc::open_disc_image(path);
  if (!opened.image || opened.image->size() < kMinDiscSize) {
    FILE* probe = std::fopen(path, "rb");
    header.io_error = probe == nullptr;
    if (probe != nullptr) {
      std::fclose(probe);
    }
    return header;
  }
  std::array<unsigned char, 0x20> hdr{};
  if (!opened.image->read(0, hdr.data(), hdr.size())) {
    return header;
  }
  if (read_be32(hdr.data() + 0x1C) == kGcMagic) {
    header.platform = Platform::GameCube;
  } else if (read_be32(hdr.data() + 0x18) == kWiiMagic) {
    header.platform = Platform::Wii;
  } else {
    return header;
  }
  header.ok = true;
  header.game_id = std::string(reinterpret_cast<const char*>(hdr.data()), 6);
  header.disc_number = hdr[6];
  header.revision = hdr[7];
  return header;
}

ValidationError inspect_switch(const char* path, DiscInfo& info) {
  const SwitchDiscHeader header = read_switch_header(path);
  if (!header.ok) {
    return header.io_error ? ValidationError::IOError : ValidationError::InvalidImage;
  }
  info.platform = header.platform;
  info.region = region_from_game_id(header.game_id);
  info.revision = header.revision;
  info.gameId = header.game_id;
  for (const auto& record : AcceptedDiscs) {
    if (record.gameId == header.game_id && record.discNumber == header.disc_number &&
        record.revision == header.revision) {
      return ValidationError::Success;
    }
  }
  for (const std::string_view id : RecognizedGameIds) {
    if (id == header.game_id) {
      return ValidationError::WrongVersion;
    }
  }
  return ValidationError::WrongGame;
}
#else
constexpr borealis::disc::Catalog DiscCatalog{
    .acceptedDiscs = AcceptedDiscs,
    .recognizedGameIds = RecognizedGameIds,
};

ValidationError validation_error(borealis::disc::Status status) noexcept {
    switch (status) {
    case borealis::disc::Status::Success:
        return ValidationError::Success;
    case borealis::disc::Status::IOError:
        return ValidationError::IOError;
    case borealis::disc::Status::InvalidImage:
        return ValidationError::InvalidImage;
    case borealis::disc::Status::UnknownGame:
        return ValidationError::WrongGame;
    case borealis::disc::Status::UnsupportedVersion:
        return ValidationError::WrongVersion;
    case borealis::disc::Status::Canceled:
        return ValidationError::Canceled;
    case borealis::disc::Status::HashMismatch:
        return ValidationError::HashMismatch;
    case borealis::disc::Status::Failed:
    default:
        return ValidationError::Unknown;
    }
}



void update_info(const borealis::disc::Result& result, DiscInfo& info) {
    if (!result.metadata.gameId.empty()) {
        info.platform = result.metadata.platform;
        info.region = region_from_game_id(result.metadata.gameId);
        info.revision = result.metadata.revision;
        info.gameId = result.metadata.gameId;
    }
}

#endif

}  // namespace

ValidationError validate(const char* path, VerificationStatus& status, DiscInfo& info) {
#ifdef __SWITCH__
    // Only check header on HOS to avoid overhead and pain
    status.bytesRead.store(1, std::memory_order_relaxed);
    status.bytesTotal.store(1, std::memory_order_relaxed);
    return inspect_switch(path, info);
#else
    const auto result = borealis::disc::verify(
        path == nullptr ? std::string_view{} : std::string_view{path}, DiscCatalog, &status);
    update_info(result, info);
    return validation_error(result.status);
#endif
}

ValidationError inspect(const char* path, DiscInfo& info) {
#ifdef __SWITCH__
    return inspect_switch(path, info);
#else
    const auto result = borealis::disc::inspect(
        path == nullptr ? std::string_view{} : std::string_view{path}, DiscCatalog);
    update_info(result, info);
    return validation_error(result.status);
#endif
}

bool isPal(const char* path) {
    DiscInfo info{};
    return inspect(path, info) == ValidationError::Success && info.region == Region::Europe;
}

void log_verification_state(std::string_view path, DiscVerificationState state) {
    const std::string pathText = path.empty() ? "<none>" : std::string(path);
    DuskLog.info(
        "Disc verification status: {} (path: {})", verification_state_name(state), pathText);
}
}  // namespace dusk::iso
