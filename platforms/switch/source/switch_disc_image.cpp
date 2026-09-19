
// Adapted from HayatoG's dusklight
#include "switch/switch_disc_image.hpp"

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <bzlib.h>
#include <lzma.h>
#include <zlib.h>
#include <zstd.h>

namespace dusk::sw::disc {
namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;

template <typename... A>
inline void disc_warn(A&&...) {}

constexpr u32 GCZ_MAGIC = 0xB10BC001;
constexpr u32 CISO_MAGIC = 0x4F534943;
constexpr u32 TGC_MAGIC = 0xA2380FAE;
constexpr u32 WBFS_MAGIC = 0x53464257;
constexpr u32 WIA_MAGIC = 0x01414957;
constexpr u32 RVZ_MAGIC = 0x015A5652;
constexpr size_t CISO_HEADER_SIZE = 0x8000;
constexpr size_t CISO_MAP_SIZE = CISO_HEADER_SIZE - sizeof(u32) - sizeof(u32);
constexpr u64 GCZ_UNCOMPRESSED_FLAG = 1ULL << 63;
constexpr u64 DISC_SECTOR_SIZE = 0x8000;
constexpr u32 WII_SECTOR_COUNT = 143432 * 2;
constexpr u32 WIA_COMPRESSED_BIT = 1u << 31;
constexpr size_t LFG_K = 521;
constexpr size_t LFG_J = 32;
constexpr size_t LFG_SEED_SIZE = 17;
constexpr size_t LFG_K_BYTES = LFG_K * sizeof(u32);



u32 readBE32(const u8* data) {
  return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
         (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
}

u16 swap16(u16 value) {
  return static_cast<u16>((value << 8) | (value >> 8));
}

u32 swap32(u32 value) {
  return ((value & 0x000000ff) << 24) | ((value & 0x0000ff00) << 8) | ((value & 0x00ff0000) >> 8) |
         ((value & 0xff000000) >> 24);
}

u64 swap64(u64 value) {
  return (static_cast<u64>(swap32(static_cast<u32>(value))) << 32) |
         static_cast<u64>(swap32(static_cast<u32>(value >> 32)));
}

u32 be32(u32 value) { return swap32(value); }
u64 be64(u64 value) { return swap64(value); }
u64 alignDown(u64 value, u64 alignment) { return value & ~(alignment - 1); }
u64 alignUp(u64 value, u64 alignment) { return (value + alignment - 1) & ~(alignment - 1); }

bool readFileAt(FILE* file, u64 offset, void* out, size_t length) {
  if (file == nullptr || out == nullptr) {
    return false;
  }
  if (length == 0) {
    return true;
  }
  if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
    return false;
  }
  return std::fread(out, 1, length, file) == length;
}

u64 fileSize(FILE* file) {
  if (file == nullptr) {
    return 0;
  }
  const long current = std::ftell(file);
  if (current < 0 || std::fseek(file, 0, SEEK_END) != 0) {
    return 0;
  }
  const long end = std::ftell(file);
  std::fseek(file, current, SEEK_SET);
  return end < 0 ? 0 : static_cast<u64>(end);
}

void replaceBytes(u64 offset, u64 size, u8* out, u64 replaceOffset, u64 replaceSize, const u8* replace) {
  if (out == nullptr || replace == nullptr) {
    return;
  }
  const u64 replaceStart = std::max(offset, replaceOffset);
  const u64 replaceEnd = std::min(offset + size, replaceOffset + replaceSize);
  if (replaceEnd > replaceStart) {
    std::memcpy(out + (replaceStart - offset), replace + (replaceStart - replaceOffset),
                static_cast<size_t>(replaceEnd - replaceStart));
  }
}

template <typename T>
void replaceValue(u64 offset, u64 size, u8* out, u64 replaceOffset, const T& value) {
  replaceBytes(offset, size, out, replaceOffset, sizeof(T), reinterpret_cast<const u8*>(&value));
}



class FileImageReader : public DiscImage {
public:
  ~FileImageReader() override {
    if (m_file != nullptr) {
      std::fclose(m_file);
      m_file = nullptr;
    }
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (offset + length < offset || offset + length > size()) {
      return false;
    }
    return readFileAt(m_file, offset, out, length);
  }

  u64 size() const override { return m_size; }

protected:
  explicit FileImageReader(FILE* file) : m_file(file), m_size(fileSize(file)) {}

  FILE* m_file = nullptr;
  u64 m_size = 0;
};

class RawImageReader final : public FileImageReader {
public:
  static std::unique_ptr<RawImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }
    return std::unique_ptr<RawImageReader>(new RawImageReader(file));
  }

private:
  explicit RawImageReader(FILE* file) : FileImageReader(file) {}
};

#pragma pack(push, 1)
struct GczHeader {
  u32 magicCookie;
  u32 subType;
  u64 compressedDataSize;
  u64 dataSize;
  u32 blockSize;
  u32 numBlocks;
};

struct TgcHeader {
  u32 magic;
  u32 unknown1;
  u32 tgcHeaderSize;
  u32 discHeaderAreaSize;
  u32 fstRealOffset;
  u32 fstSize;
  u32 fstMaxSize;
  u32 dolRealOffset;
  u32 dolSize;
  u32 fileAreaRealOffset;
  u32 unknown2;
  u32 unknown3;
  u32 unknown4;
  u32 fileAreaVirtualOffset;
};
#pragma pack(pop)

class GczImageReader final : public FileImageReader {
public:
  static std::unique_ptr<GczImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }

    GczHeader header{};
    if (!readFileAt(file, 0, &header, sizeof(header)) || header.magicCookie != GCZ_MAGIC || header.blockSize == 0 ||
        header.numBlocks == 0 || header.dataSize == 0 ||
        header.numBlocks != (header.dataSize + header.blockSize - 1) / header.blockSize) {
      std::fclose(file);
      return nullptr;
    }

    std::vector<u64> blockPointers(header.numBlocks);
    if (!readFileAt(file, sizeof(header), blockPointers.data(), blockPointers.size() * sizeof(u64))) {
      std::fclose(file);
      return nullptr;
    }

    return std::unique_ptr<GczImageReader>(new GczImageReader(file, header, std::move(blockPointers)));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > m_header.dataSize) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      const u64 block = offset / m_header.blockSize;
      const u64 blockOffset = offset % m_header.blockSize;
      const size_t copySize =
          static_cast<size_t>(std::min<u64>(m_header.blockSize - blockOffset, remaining));
      if (!loadBlock(block)) {
        return false;
      }
      std::memcpy(outBytes, m_blockCache.data() + blockOffset, copySize);
      outBytes += copySize;
      offset += copySize;
      remaining -= copySize;
    }
    return true;
  }

  u64 size() const override { return m_header.dataSize; }

private:
  GczImageReader(FILE* file, const GczHeader& header, std::vector<u64> blockPointers)
      : FileImageReader(file), m_header(header), m_blockPointers(std::move(blockPointers)),
        m_dataOffset(sizeof(GczHeader) + m_blockPointers.size() * (sizeof(u64) + sizeof(u32))),
        m_zlibBuffer(m_header.blockSize + 64), m_blockCache(m_header.blockSize) {}

  bool loadBlock(u64 block) {
    if (block == m_cachedBlock) {
      return true;
    }
    if (block >= m_blockPointers.size()) {
      return false;
    }

    const u64 rawStart = m_blockPointers[block];
    const bool uncompressed = (rawStart & GCZ_UNCOMPRESSED_FLAG) != 0;
    const u64 start = rawStart & ~GCZ_UNCOMPRESSED_FLAG;
    const u64 rawEnd = block + 1 < m_blockPointers.size() ? m_blockPointers[block + 1] : m_header.compressedDataSize;
    const u64 end = rawEnd & ~GCZ_UNCOMPRESSED_FLAG;
    if (end < start || end - start > m_zlibBuffer.size()) {
      return false;
    }

    const size_t compressedSize = static_cast<size_t>(end - start);
    if (!readFileAt(m_file, m_dataOffset + start, m_zlibBuffer.data(), compressedSize)) {
      return false;
    }

    if (uncompressed) {
      if (compressedSize != m_header.blockSize) {
        return false;
      }
      std::memcpy(m_blockCache.data(), m_zlibBuffer.data(), compressedSize);
    } else {
      z_stream stream{};
      stream.next_in = m_zlibBuffer.data();
      stream.avail_in = static_cast<uInt>(compressedSize);
      stream.next_out = m_blockCache.data();
      stream.avail_out = m_header.blockSize;
      if (inflateInit(&stream) != Z_OK) {
        return false;
      }
      const int status = inflate(&stream, Z_FULL_FLUSH);
      const bool ok = status == Z_STREAM_END && stream.avail_out == 0;
      inflateEnd(&stream);
      if (!ok) {
        return false;
      }
    }

    m_cachedBlock = block;
    return true;
  }

  GczHeader m_header{};
  std::vector<u64> m_blockPointers;
  u64 m_dataOffset = 0;
  std::vector<u8> m_zlibBuffer;
  std::vector<u8> m_blockCache;
  u64 m_cachedBlock = std::numeric_limits<u64>::max();
};

class CisoImageReader final : public FileImageReader {
public:
  static std::unique_ptr<CisoImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }

    u32 magic = 0;
    u32 blockSize = 0;
    std::vector<u8> rawMap(CISO_MAP_SIZE);
    if (!readFileAt(file, 0, &magic, sizeof(magic)) || magic != CISO_MAGIC ||
        !readFileAt(file, sizeof(magic), &blockSize, sizeof(blockSize)) || blockSize == 0 ||
        !readFileAt(file, sizeof(magic) + sizeof(blockSize), rawMap.data(), rawMap.size())) {
      std::fclose(file);
      return nullptr;
    }

    std::vector<u16> blockMap(CISO_MAP_SIZE);
    u16 usedBlockCount = 0;
    for (size_t i = 0; i < rawMap.size(); ++i) {
      blockMap[i] = rawMap[i] == 1 ? usedBlockCount++ : std::numeric_limits<u16>::max();
    }

    return std::unique_ptr<CisoImageReader>(new CisoImageReader(file, blockSize, std::move(blockMap)));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      const u64 block = offset / m_blockSize;
      const u64 blockOffset = offset % m_blockSize;
      const size_t readSize = static_cast<size_t>(std::min<u64>(m_blockSize - blockOffset, remaining));
      if (block < m_blockMap.size() && m_blockMap[block] != std::numeric_limits<u16>::max()) {
        const u64 fileOffset = CISO_HEADER_SIZE + static_cast<u64>(m_blockMap[block]) * m_blockSize + blockOffset;
        if (!readFileAt(m_file, fileOffset, outBytes, readSize)) {
          return false;
        }
      } else {
        std::memset(outBytes, 0, readSize);
      }
      outBytes += readSize;
      offset += readSize;
      remaining -= readSize;
    }
    return true;
  }

  u64 size() const override { return static_cast<u64>(m_blockMap.size()) * m_blockSize; }

private:
  CisoImageReader(FILE* file, u32 blockSize, std::vector<u16> blockMap)
      : FileImageReader(file), m_blockSize(blockSize), m_blockMap(std::move(blockMap)) {}

  u32 m_blockSize = 0;
  std::vector<u16> m_blockMap;
};

class TgcImageReader final : public FileImageReader {
public:
  static std::unique_ptr<TgcImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }

    TgcHeader header{};
    if (!readFileAt(file, 0, &header, sizeof(header)) || header.magic != TGC_MAGIC) {
      std::fclose(file);
      return nullptr;
    }

    return std::unique_ptr<TgcImageReader>(new TgcImageReader(file, header));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }
    const u32 tgcHeaderSize = swap32(m_header.tgcHeaderSize);
    auto* outBytes = static_cast<u8*>(out);
    if (!readFileAt(m_file, offset + tgcHeaderSize, outBytes, length)) {
      return false;
    }

    const u32 replacementDolOffset = swap32(swap32(m_header.dolRealOffset) - tgcHeaderSize);
    const u32 replacementFstOffset = swap32(swap32(m_header.fstRealOffset) - tgcHeaderSize);
    replaceValue(offset, length, outBytes, 0x420, replacementDolOffset);
    replaceValue(offset, length, outBytes, 0x424, replacementFstOffset);
    replaceBytes(offset, length, outBytes, swap32(replacementFstOffset), m_fst.size(), m_fst.data());
    return true;
  }

  u64 size() const override {
    const u32 tgcHeaderSize = swap32(m_header.tgcHeaderSize);
    return m_size > tgcHeaderSize ? m_size - tgcHeaderSize : 0;
  }

private:
  TgcImageReader(FILE* file, const TgcHeader& header) : FileImageReader(file), m_header(header) {
    const u32 fstOffset = swap32(m_header.fstRealOffset);
    const u32 fstSize = swap32(m_header.fstSize);
    if (fstSize == 0 || fstSize > 64 * 1024 * 1024) {
      return;
    }

    m_fst.resize(fstSize);
    if (!readFileAt(m_file, fstOffset, m_fst.data(), m_fst.size()) || m_fst.size() < 12) {
      m_fst.clear();
      return;
    }

    const u32 fileAreaShift =
        swap32(m_header.fileAreaRealOffset) - swap32(m_header.fileAreaVirtualOffset) - swap32(m_header.tgcHeaderSize);
    const size_t fstEntries = std::min<size_t>(readBE32(m_fst.data() + 8), m_fst.size() / 12);
    for (size_t i = 0; i < fstEntries; ++i) {
      if (m_fst[i * 12] != 0) {
        continue;
      }
      const u32 oldOffset = readBE32(m_fst.data() + i * 12 + 4);
      const u32 newOffset = swap32(oldOffset + fileAreaShift);
      replaceValue(0, m_fst.size(), m_fst.data(), i * 12 + 4, newOffset);
    }
  }

  TgcHeader m_header{};
  std::vector<u8> m_fst;
};

class WbfsImageReader final : public DiscImage {
public:
  ~WbfsImageReader() override {
    for (auto& file : m_files) {
      if (file.file != nullptr) {
        std::fclose(file.file);
        file.file = nullptr;
      }
    }
  }

  static std::unique_ptr<WbfsImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }
    auto reader = std::unique_ptr<WbfsImageReader>(new WbfsImageReader());
    if (!reader->addFile(file) || !reader->openAdditionalFiles(imagePath) || !reader->readHeader()) {
      return nullptr;
    }
    return reader;
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      const u64 block = offset / m_wbfsSectorSize;
      const u64 blockOffset = offset & (m_wbfsSectorSize - 1);
      const size_t copySize = static_cast<size_t>(std::min<u64>(m_wbfsSectorSize - blockOffset, remaining));

      if (block >= m_wlbaTable.size() || m_wlbaTable[block] == 0) {
        std::memset(outBytes, 0, copySize);
      } else {
        const u64 physicalOffset = static_cast<u64>(m_wlbaTable[block]) * m_wbfsSectorSize + blockOffset;
        if (!readPhysicalAt(physicalOffset, outBytes, copySize)) {
          return false;
        }
      }

      outBytes += copySize;
      offset += copySize;
      remaining -= copySize;
    }
    return true;
  }

  u64 size() const override { return WII_SECTOR_COUNT * DISC_SECTOR_SIZE; }

private:
  struct FilePart {
    FILE* file = nullptr;
    u64 base = 0;
    u64 size = 0;
  };

  bool addFile(FILE* file) {
    const u64 size = fileSize(file);
    if (size == 0) {
      std::fclose(file);
      return false;
    }
    m_files.push_back({file, m_rawSize, size});
    m_rawSize += size;
    return true;
  }

  bool openAdditionalFiles(const std::filesystem::path& imagePath) {
    std::string path = imagePath.generic_string();
    if (path.size() < 4) {
      return true;
    }
    for (size_t i = 1; i < 10; ++i) {
      std::string nextPath = path;
      nextPath.back() = static_cast<char>('0' + i);
      FILE* next = std::fopen(nextPath.c_str(), "rb");
      if (next == nullptr) {
        break;
      }
      if (!addFile(next)) {
        return false;
      }
    }
    return true;
  }

  bool readHeader() {
    if (m_files.empty()) {
      return false;
    }

#pragma pack(push, 1)
    struct WbfsHeader {
      u32 magic;
      u32 hdSectorCount;
      u8 hdSectorShift;
      u8 wbfsSectorShift;
      u8 padding[2];
      u8 discTable[500];
    };
#pragma pack(pop)

    WbfsHeader header{};
    if (!readFileAt(m_files[0].file, 0, &header, sizeof(header)) || header.magic != WBFS_MAGIC) {
      return false;
    }

    m_hdSectorSize = 1ull << header.hdSectorShift;
    m_wbfsSectorSize = 1ull << header.wbfsSectorShift;
    const u64 expectedRawSize = static_cast<u64>(be32(header.hdSectorCount)) * m_hdSectorSize;
    if (m_hdSectorSize == 0 || m_wbfsSectorSize < DISC_SECTOR_SIZE || expectedRawSize != m_rawSize ||
        header.discTable[0] == 0) {
      return false;
    }

    m_blocksPerDisc = alignUp(WII_SECTOR_COUNT * DISC_SECTOR_SIZE, m_wbfsSectorSize) / m_wbfsSectorSize;
    m_wlbaTable.resize(m_blocksPerDisc);
    if (!readFileAt(m_files[0].file, m_hdSectorSize + 0x100, m_wlbaTable.data(),
                    m_wlbaTable.size() * sizeof(u16))) {
      return false;
    }
    for (auto& entry : m_wlbaTable) {
      entry = swap16(entry);
    }
    return true;
  }

  bool readPhysicalAt(u64 offset, void* out, size_t length) {
    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      auto it = std::find_if(m_files.begin(), m_files.end(), [offset](const FilePart& part) {
        return offset >= part.base && offset < part.base + part.size;
      });
      if (it == m_files.end()) {
        return false;
      }
      const u64 fileOffset = offset - it->base;
      const size_t chunk = static_cast<size_t>(std::min<u64>(remaining, it->size - fileOffset));
      if (!readFileAt(it->file, fileOffset, outBytes, chunk)) {
        return false;
      }
      outBytes += chunk;
      offset += chunk;
      remaining -= chunk;
    }
    return true;
  }

  std::vector<FilePart> m_files;
  std::vector<u16> m_wlbaTable;
  u64 m_rawSize = 0;
  u64 m_hdSectorSize = 0;
  u64 m_wbfsSectorSize = 0;
  u64 m_blocksPerDisc = 0;
};

#pragma pack(push, 1)
struct WiaFileHeader {
  u32 magic;
  u32 version;
  u32 versionCompatible;
  u32 discSize;
  u8 discHash[20];
  u64 isoFileSize;
  u64 wiaFileSize;
  u8 fileHeadHash[20];
};

struct WiaDisc {
  u32 discType;
  u32 compression;
  s32 compressionLevel;
  u32 chunkSize;
  u8 discHead[0x80];
  u32 numPartitions;
  u32 partitionTypeSize;
  u64 partitionOffset;
  u8 partitionHash[20];
  u32 numRawData;
  u64 rawDataOffset;
  u32 rawDataSize;
  u32 numGroups;
  u64 groupOffset;
  u32 groupSize;
  u8 comprDataLen;
  u8 comprData[7];
};

struct WiaRawData {
  u64 rawDataOffset;
  u64 rawDataSize;
  u32 groupIndex;
  u32 numGroups;
};

struct WiaGroup {
  u32 dataOffset;
  u32 dataSize;
};

struct RvzGroup {
  u32 dataOffset;
  u32 dataSizeAndFlag;
  u32 rvzPackedSize;
};
#pragma pack(pop)

static_assert(sizeof(WiaFileHeader) == 0x48);
static_assert(sizeof(WiaDisc) == 0xdc);
static_assert(sizeof(WiaRawData) == 0x18);
static_assert(sizeof(WiaGroup) == 0x08);
static_assert(sizeof(RvzGroup) == 0x0c);

enum class WiaCompression : u32 {
  None = 0,
  Purge = 1,
  Bzip2 = 2,
  Lzma = 3,
  Lzma2 = 4,
  Zstandard = 5,
};

class LaggedFibonacci {
public:
  bool initWithSeedBytes(const u8* bytes, size_t length) {
    if (bytes == nullptr || length < LFG_SEED_SIZE * sizeof(u32)) {
      return false;
    }
    for (size_t i = 0; i < LFG_SEED_SIZE; ++i) {
      m_buffer[i] = readBE32(bytes + i * sizeof(u32));
    }
    m_position = 0;
    init();
    return true;
  }

  void skip(size_t bytes) {
    m_position += bytes;
    while (m_position >= LFG_K_BYTES) {
      forward();
      m_position -= LFG_K_BYTES;
    }
  }

  void fill(u8* out, size_t length) {
    while (length != 0) {
      while (m_position >= LFG_K_BYTES) {
        forward();
        m_position -= LFG_K_BYTES;
      }
      const auto* bytes = reinterpret_cast<const u8*>(m_buffer.data());
      const size_t chunk = std::min(length, LFG_K_BYTES - m_position);
      std::memcpy(out, bytes + m_position, chunk);
      out += chunk;
      length -= chunk;
      m_position += chunk;
    }
  }

private:
  void init() {
    for (size_t i = LFG_SEED_SIZE; i < LFG_K; ++i) {
      m_buffer[i] = (m_buffer[i - LFG_SEED_SIZE] << 23) ^ (m_buffer[i - LFG_SEED_SIZE + 1] >> 9) ^
                    m_buffer[i - 1];
    }
    for (auto& value : m_buffer) {
      value = swap32((value & 0xff00ffff) | ((value >> 2) & 0x00ff0000));
    }
    for (size_t i = 0; i < 4; ++i) {
      forward();
    }
  }

  void forward() {
    for (size_t i = 0; i < LFG_J; ++i) {
      m_buffer[i] ^= m_buffer[i + LFG_K - LFG_J];
    }
    for (size_t i = LFG_J; i < LFG_K; ++i) {
      m_buffer[i] ^= m_buffer[i - LFG_J];
    }
  }

  std::array<u32, LFG_K> m_buffer{};
  size_t m_position = 0;
};

bool decodeLzmaProps(const u8* props, size_t propsLen, lzma_options_lzma* options) {
  if (props == nullptr || propsLen != 5 || options == nullptr || lzma_lzma_preset(options, LZMA_PRESET_DEFAULT)) {
    return false;
  }
  u32 value = props[0];
  if (value >= 9 * 5 * 5) {
    return false;
  }
  options->lc = value % 9;
  value /= 9;
  options->pb = value / 5;
  options->lp = value % 5;
  options->dict_size = static_cast<u32>(props[1]) | (static_cast<u32>(props[2]) << 8) |
                       (static_cast<u32>(props[3]) << 16) | (static_cast<u32>(props[4]) << 24);
  return true;
}

bool decodeLzma2Props(const u8* props, size_t propsLen, lzma_options_lzma* options) {
  if (props == nullptr || propsLen != 1 || options == nullptr || lzma_lzma_preset(options, LZMA_PRESET_DEFAULT)) {
    return false;
  }
  const u32 value = props[0];
  if (value > 40) {
    return false;
  }
  options->dict_size = value == 40 ? UINT32_MAX : ((2 | (value & 1)) << (value / 2 + 11));
  return true;
}

bool decompressLzmaRaw(lzma_vli filterId, const u8* props, size_t propsLen, const u8* in, size_t inSize, u8* out,
                       size_t outCapacity, size_t* outSize) {
  lzma_options_lzma options{};
  if ((filterId == LZMA_FILTER_LZMA1 && !decodeLzmaProps(props, propsLen, &options)) ||
      (filterId == LZMA_FILTER_LZMA2 && !decodeLzma2Props(props, propsLen, &options))) {
    return false;
  }

  lzma_filter filters[2] = {{filterId, &options}, {LZMA_VLI_UNKNOWN, nullptr}};
  size_t inPos = 0;
  size_t outPos = 0;
  const lzma_ret ret = lzma_raw_buffer_decode(filters, nullptr, in, &inPos, inSize, out, &outPos, outCapacity);
  if (ret != LZMA_OK || inPos != inSize) {
    return false;
  }
  if (outSize != nullptr) {
    *outSize = outPos;
  }
  return true;
}

bool decompressWia(WiaCompression compression, const u8* props, size_t propsLen, const u8* in, size_t inSize, u8* out,
                   size_t outCapacity, size_t* outSize) {
  if (out == nullptr || (in == nullptr && inSize != 0)) {
    return false;
  }

  switch (compression) {
  case WiaCompression::None:
  case WiaCompression::Purge:
    if (inSize > outCapacity) {
      return false;
    }
    std::memcpy(out, in, inSize);
    if (outSize != nullptr) {
      *outSize = inSize;
    }
    return true;
  case WiaCompression::Bzip2: {
    unsigned int len = static_cast<unsigned int>(outCapacity);
    if (outCapacity > std::numeric_limits<unsigned int>::max() || inSize > std::numeric_limits<unsigned int>::max()) {
      return false;
    }
    const int ret = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(out), &len, const_cast<char*>(
                                                   reinterpret_cast<const char*>(in)),
                                               static_cast<unsigned int>(inSize), 0, 0);
    if (ret != BZ_OK) {
      return false;
    }
    if (outSize != nullptr) {
      *outSize = len;
    }
    return true;
  }
  case WiaCompression::Lzma:
    return decompressLzmaRaw(LZMA_FILTER_LZMA1, props, propsLen, in, inSize, out, outCapacity, outSize);
  case WiaCompression::Lzma2:
    return decompressLzmaRaw(LZMA_FILTER_LZMA2, props, propsLen, in, inSize, out, outCapacity, outSize);
  case WiaCompression::Zstandard: {
    const size_t ret = ZSTD_decompress(out, outCapacity, in, inSize);
    if (ZSTD_isError(ret)) {
      return false;
    }
    if (outSize != nullptr) {
      *outSize = ret;
    }
    return true;
  }
  }
  return false;
}

class WiaImageReader final : public FileImageReader {
public:
  static std::unique_ptr<WiaImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      disc_warn("WIA/RVZ open failed: fopen '{}'", imagePath.generic_string());
      return nullptr;
    }

    WiaFileHeader header{};
    if (!readFileAt(file, 0, &header, sizeof(header)) || (header.magic != WIA_MAGIC && header.magic != RVZ_MAGIC)) {
      disc_warn("WIA/RVZ open failed: invalid header '{}'", imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }

    WiaDisc disc{};
    const u32 discSize = be32(header.discSize);
    if (discSize == 0 || discSize > 1024) {
      disc_warn("WIA/RVZ open failed: invalid disc header size {} '{}'", discSize, imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }
    std::vector<u8> discBuffer(discSize);
    if (!readFileAt(file, sizeof(WiaFileHeader), discBuffer.data(), discBuffer.size())) {
      disc_warn("WIA/RVZ open failed: could not read disc header '{}'", imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }
    std::memcpy(&disc, discBuffer.data(), std::min(sizeof(disc), discBuffer.size()));

    const u32 chunkSize = be32(disc.chunkSize);
    const u32 numRawData = be32(disc.numRawData);
    const u32 numGroups = be32(disc.numGroups);
    const auto compression = static_cast<WiaCompression>(be32(disc.compression));
    if (chunkSize < DISC_SECTOR_SIZE || chunkSize % DISC_SECTOR_SIZE != 0 || numRawData == 0 || numGroups == 0 ||
        numRawData > 1024 * 1024 || numGroups > 16 * 1024 * 1024 || disc.comprDataLen > sizeof(disc.comprData)) {
      disc_warn("WIA/RVZ open failed: invalid tables chunk={} raw={} groups={} compData={} '{}'", chunkSize,
               numRawData, numGroups, static_cast<unsigned>(disc.comprDataLen), imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }

    std::vector<WiaRawData> rawData(numRawData);
    if (!readTable(file, compression, disc.comprData, disc.comprDataLen, be64(disc.rawDataOffset),
                   be32(disc.rawDataSize), rawData.data(), rawData.size() * sizeof(WiaRawData))) {
      disc_warn("WIA/RVZ open failed: raw data table offset={} size={} out={} '{}'", be64(disc.rawDataOffset),
               be32(disc.rawDataSize), rawData.size() * sizeof(WiaRawData), imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }

    std::vector<RvzGroup> groups(numGroups);
    if (header.magic == RVZ_MAGIC) {
      if (!readTable(file, compression, disc.comprData, disc.comprDataLen, be64(disc.groupOffset), be32(disc.groupSize),
                     groups.data(), groups.size() * sizeof(RvzGroup))) {
        disc_warn("RVZ open failed: group table offset={} size={} out={} '{}'", be64(disc.groupOffset),
                 be32(disc.groupSize), groups.size() * sizeof(RvzGroup), imagePath.generic_string());
        std::fclose(file);
        return nullptr;
      }
    } else {
      std::vector<WiaGroup> wiaGroups(numGroups);
      if (!readTable(file, compression, disc.comprData, disc.comprDataLen, be64(disc.groupOffset), be32(disc.groupSize),
                     wiaGroups.data(), wiaGroups.size() * sizeof(WiaGroup))) {
        disc_warn("WIA open failed: group table offset={} size={} out={} '{}'", be64(disc.groupOffset),
                 be32(disc.groupSize), wiaGroups.size() * sizeof(WiaGroup), imagePath.generic_string());
        std::fclose(file);
        return nullptr;
      }
      for (size_t i = 0; i < groups.size(); ++i) {
        groups[i].dataOffset = wiaGroups[i].dataOffset;
        groups[i].dataSizeAndFlag = swap32(be32(wiaGroups[i].dataSize) | WIA_COMPRESSED_BIT);
        groups[i].rvzPackedSize = 0;
      }
    }

    return std::unique_ptr<WiaImageReader>(
        new WiaImageReader(file, header, disc, compression, std::move(rawData), std::move(groups)));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      if (offset < m_cacheStart || offset >= m_cacheStart + m_cacheSize) {
        if (!loadGroupForOffset(offset)) {
          return false;
        }
      }
      const u64 cacheOffset = offset - m_cacheStart;
      const size_t copySize = static_cast<size_t>(std::min<u64>(remaining, m_cacheSize - cacheOffset));
      std::memcpy(outBytes, m_cache.data() + cacheOffset, copySize);
      outBytes += copySize;
      offset += copySize;
      remaining -= copySize;
    }
    return true;
  }

  u64 size() const override { return be64(m_header.isoFileSize); }

private:
  struct GroupInfo {
    u32 index = 0;
    u32 sector = 0;
    u32 numSectors = 0;
    u32 size = 0;
    u64 sectionOffset = 0;
  };

  WiaImageReader(FILE* file, const WiaFileHeader& header, const WiaDisc& disc, WiaCompression compression,
                 std::vector<WiaRawData> rawData, std::vector<RvzGroup> groups)
      : FileImageReader(file), m_header(header), m_disc(disc), m_compression(compression),
        m_rawData(std::move(rawData)), m_groups(std::move(groups)), m_cache(be32(m_disc.chunkSize)) {}

  static bool readTable(FILE* file, WiaCompression compression, const u8* props, size_t propsLen, u64 offset,
                        size_t compressedSize, void* out, size_t outSize) {
    if (compressedSize == 0 || out == nullptr || outSize == 0) {
      return false;
    }
    std::vector<u8> compressed(compressedSize);
    if (!readFileAt(file, offset, compressed.data(), compressed.size())) {
      return false;
    }
    size_t actual = 0;
    return decompressWia(compression, props, propsLen, compressed.data(), compressed.size(), static_cast<u8*>(out),
                         outSize, &actual) &&
           actual == outSize;
  }

  bool findGroupInfoForSector(u32 sector, GroupInfo* out) const {
    const u32 chunkSize = be32(m_disc.chunkSize);
    const u32 sectorsPerChunk = chunkSize / DISC_SECTOR_SIZE;
    for (const auto& raw : m_rawData) {
      const u64 startOffset = alignDown(be64(raw.rawDataOffset), DISC_SECTOR_SIZE);
      const u64 endOffset = be64(raw.rawDataOffset) + be64(raw.rawDataSize);
      const u32 startSector = static_cast<u32>(startOffset / DISC_SECTOR_SIZE);
      const u32 endSector = static_cast<u32>(alignUp(endOffset, DISC_SECTOR_SIZE) / DISC_SECTOR_SIZE);
      if (sector < startSector || sector >= endSector) {
        continue;
      }

      const u32 relGroup = (sector - startSector) / sectorsPerChunk;
      const u32 groupIndex = be32(raw.groupIndex) + relGroup;
      if (groupIndex >= m_groups.size()) {
        return false;
      }
      const u32 groupSector = startSector + relGroup * sectorsPerChunk;
      const u64 groupOffset = static_cast<u64>(groupSector) * DISC_SECTOR_SIZE;
      const u32 groupSize = static_cast<u32>(std::min<u64>(endOffset - groupOffset, chunkSize));
      if (out != nullptr) {
        *out = {groupIndex, groupSector, static_cast<u32>(alignUp(groupSize, DISC_SECTOR_SIZE) / DISC_SECTOR_SIZE),
                groupSize, groupOffset};
      }
      return true;
    }
    return false;
  }

  bool rvzUnpack(const u8* data, size_t dataSize, u8* out, const GroupInfo& info, size_t* outSize) {
    size_t inPos = 0;
    size_t outPos = 0;
    LaggedFibonacci lfg;
    if (outSize != nullptr) {
      *outSize = 0;
    }
    while (inPos + 4 <= dataSize) {
      const u32 rawSize = readBE32(data + inPos);
      inPos += 4;
      const bool junk = (rawSize & WIA_COMPRESSED_BIT) != 0;
      const u32 size = rawSize & ~WIA_COMPRESSED_BIT;
      if (outPos + size > info.size) {
        if (outSize != nullptr) {
          *outSize = outPos;
        }
        return false;
      }
      if (junk) {
        if (inPos + LFG_SEED_SIZE * sizeof(u32) > dataSize ||
            !lfg.initWithSeedBytes(data + inPos, LFG_SEED_SIZE * sizeof(u32))) {
          if (outSize != nullptr) {
            *outSize = outPos;
          }
          return false;
        }
        inPos += LFG_SEED_SIZE * sizeof(u32);
        lfg.skip(static_cast<size_t>((info.sectionOffset + outPos) % DISC_SECTOR_SIZE));
        lfg.fill(out + outPos, size);
      } else {
        if (inPos + size > dataSize) {
          return false;
        }
        std::memcpy(out + outPos, data + inPos, size);
        inPos += size;
      }
      outPos += size;
    }
    if (outSize != nullptr) {
      *outSize = outPos;
    }
    return inPos == dataSize && outPos == info.size;
  }

  bool loadGroupForOffset(u64 offset) {
    GroupInfo info{};
    if (!findGroupInfoForSector(static_cast<u32>(offset / DISC_SECTOR_SIZE), &info) || info.size > m_cache.size()) {
      disc_warn("WIA/RVZ read failed: no group for offset {} cache={} chunk={}", offset, m_cache.size(),
               be32(m_disc.chunkSize));
      return false;
    }

    const auto& group = m_groups[info.index];
    const u32 groupDataSize = be32(group.dataSizeAndFlag) & ~WIA_COMPRESSED_BIT;
    const bool compressed = (be32(group.dataSizeAndFlag) & WIA_COMPRESSED_BIT) != 0;
    const u32 rvzPackedSize = be32(group.rvzPackedSize);
    std::fill(m_cache.begin(), m_cache.end(), 0);

    if (groupDataSize != 0) {
      std::vector<u8> groupData(groupDataSize);
      if (!readFileAt(m_file, static_cast<u64>(be32(group.dataOffset)) * 4, groupData.data(), groupData.size())) {
        disc_warn("WIA/RVZ read failed: could not read group {} fileOff={} size={}", info.index,
                 static_cast<u64>(be32(group.dataOffset)) * 4, groupData.size());
        return false;
      }

      std::vector<u8> unpacked;
      const u8* source = groupData.data();
      size_t sourceSize = groupData.size();
      if (compressed) {
        const size_t maxSize = rvzPackedSize != 0 ? rvzPackedSize : info.size;
        unpacked.resize(maxSize);
        size_t actual = 0;
        if (!decompressWia(m_compression, m_disc.comprData, m_disc.comprDataLen, groupData.data(), groupData.size(),
                           unpacked.data(), unpacked.size(), &actual)) {
          disc_warn("WIA/RVZ read failed: decompress group {} compressedSize={} maxOut={} rvzPacked={} comp={}",
                   info.index, groupData.size(), unpacked.size(), rvzPackedSize, static_cast<u32>(m_compression));
          return false;
        }
        unpacked.resize(actual);
        source = unpacked.data();
        sourceSize = unpacked.size();
      }

      if (rvzPackedSize != 0) {
        size_t unpackedSize = 0;
        if (!rvzUnpack(source, sourceSize, m_cache.data(), info, &unpackedSize)) {
          disc_warn("RVZ read failed: unpack group {} source={} produced={} expected={} sectionOffset={}", info.index,
                   sourceSize, unpackedSize, info.size, info.sectionOffset);
          return false;
        }
      } else {
        if (sourceSize != info.size) {
          disc_warn("WIA/RVZ read failed: group {} size mismatch source={} expected={}", info.index, sourceSize,
                   info.size);
          return false;
        }
        std::memcpy(m_cache.data(), source, sourceSize);
      }
    }

    if (info.sector == 0) {
      std::memcpy(m_cache.data(), m_disc.discHead, sizeof(m_disc.discHead));
    }

    m_cacheStart = static_cast<u64>(info.sector) * DISC_SECTOR_SIZE;
    m_cacheSize = info.size;
    return true;
  }

  WiaFileHeader m_header{};
  WiaDisc m_disc{};
  WiaCompression m_compression = WiaCompression::None;
  std::vector<WiaRawData> m_rawData;
  std::vector<RvzGroup> m_groups;
  std::vector<u8> m_cache;
  u64 m_cacheStart = std::numeric_limits<u64>::max();
  u64 m_cacheSize = 0;
};

std::unique_ptr<DiscImage> createDiscImage(const std::filesystem::path& imagePath) {
  FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
  if (file == nullptr) {
    return nullptr;
  }

  u32 magic = 0;
  const bool haveMagic = readFileAt(file, 0, &magic, sizeof(magic));
  std::fclose(file);
  if (!haveMagic) {
    return nullptr;
  }

  switch (magic) {
  case GCZ_MAGIC:
    return GczImageReader::create(imagePath);
  case CISO_MAGIC:
    return CisoImageReader::create(imagePath);
  case TGC_MAGIC:
    return TgcImageReader::create(imagePath);
  case WBFS_MAGIC:
    return WbfsImageReader::create(imagePath);
  case WIA_MAGIC:
  case RVZ_MAGIC:
    return WiaImageReader::create(imagePath);
  default:
    return RawImageReader::create(imagePath);
  }
}


}  // namespace

OpenResult open_disc_image(const std::string& path) noexcept {
  if (path.empty()) {
    return OpenResult{nullptr, std::string("empty disc path")};
  }
  std::unique_ptr<DiscImage> image = createDiscImage(std::filesystem::path(path));
  if (!image) {
    return OpenResult{nullptr, std::string("unsupported or unreadable disc image")};
  }
  return OpenResult{std::move(image), {}};
}

bool is_container_magic(std::uint32_t le_magic) noexcept {
  switch (le_magic) {
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

}  // namespace dusk::sw::disc
