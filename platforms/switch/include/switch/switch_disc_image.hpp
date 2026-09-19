#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace dusk::sw::disc {

class DiscImage {
 public:
  virtual ~DiscImage() = default;
  virtual bool read(std::uint64_t offset, void* out, std::size_t length) = 0;
  virtual std::uint64_t size() const = 0;
};

struct OpenResult {
  std::unique_ptr<DiscImage> image;
  std::string error;
};

OpenResult open_disc_image(const std::string& path) noexcept;
bool is_container_magic(std::uint32_t le_magic) noexcept;

}  // namespace dusk::sw::disc
