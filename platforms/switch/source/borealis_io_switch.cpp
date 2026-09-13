#include <borealis/io.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace borealis::io {
namespace {

std::filesystem::path to_path(std::string_view location) {
  return fs_path_from_utf8(location);
}

void set_error(std::string& out, std::string_view action) {
  out = std::string(action) + ": " + std::strerror(errno);
}

Status errno_status() {
  if (errno == ENOENT)
    return Status::NotFound;
  if (errno == EEXIST)
    return Status::AlreadyExists;
  return Status::Failed;
}

const char* fopen_mode(File::Mode mode) {
  switch (mode) {
  case File::Mode::Read:
    return "rb";
  case File::Mode::Truncate:
    return "wb";
  case File::Mode::Append:
    return "ab";
  }
  return "rb";
}

FILE* as_file(SDL_IOStream* handle) {
  return reinterpret_cast<FILE*>(handle);
}

}  // namespace

File::~File() {
  close();
}

File::File(File&& other) noexcept
    : m_handle{std::exchange(other.m_handle, nullptr)},
      m_access{std::exchange(other.m_access, nullptr)},
      m_writable{std::exchange(other.m_writable, false)},
      m_error{std::move(other.m_error)} {}

File& File::operator=(File&& other) noexcept {
  if (this != &other) {
    close();
    m_handle = std::exchange(other.m_handle, nullptr);
    m_access = std::exchange(other.m_access, nullptr);
    m_writable = std::exchange(other.m_writable, false);
    m_error = std::move(other.m_error);
  }
  return *this;
}

uint64_t File::size() const noexcept {
  FILE* file = as_file(m_handle);
  if (file == nullptr)
    return 0;
  const long current = std::ftell(file);
  if (current < 0)
    return 0;
  if (std::fseek(file, 0, SEEK_END) != 0)
    return 0;
  const long end = std::ftell(file);
  std::fseek(file, current, SEEK_SET);
  return end < 0 ? 0 : static_cast<uint64_t>(end);
}

uint64_t File::read(void* buf, uint64_t len) noexcept {
  FILE* file = as_file(m_handle);
  if (file == nullptr || buf == nullptr)
    return 0;
  const size_t got = std::fread(buf, 1, static_cast<size_t>(len), file);
  if (got == 0 && std::ferror(file))
    set_sdl_error("read");
  return static_cast<uint64_t>(got);
}

bool File::seek(uint64_t offset) noexcept {
  FILE* file = as_file(m_handle);
  if (file == nullptr)
    return false;
  if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
    set_sdl_error("seek");
    return false;
  }
  return true;
}

bool File::write(std::span<const std::byte> bytes) noexcept {
  FILE* file = as_file(m_handle);
  if (file == nullptr || !m_writable)
    return false;
  if (!bytes.empty() &&
      std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
    set_sdl_error("write");
    return false;
  }
  return true;
}

bool File::flush() noexcept {
  FILE* file = as_file(m_handle);
  if (file == nullptr)
    return false;
  if (std::fflush(file) != 0) {
    set_sdl_error("flush");
    return false;
  }
  return true;
}

bool File::close() noexcept {
  FILE* file = as_file(m_handle);
  m_handle = nullptr;
  if (file == nullptr)
    return true;
  if (std::fclose(file) != 0) {
    set_sdl_error("close");
    return false;
  }
  return true;
}

void File::set_sdl_error(std::string_view action) noexcept {
  set_error(m_error, action);
}

RandomAccessFile::~RandomAccessFile() {
  close();
}

RandomAccessFile::RandomAccessFile(RandomAccessFile&& other) noexcept
    : m_handle{std::exchange(other.m_handle, -1)}, m_size{other.m_size} {}

RandomAccessFile& RandomAccessFile::operator=(RandomAccessFile&& other) noexcept {
  if (this != &other) {
    close();
    m_handle = std::exchange(other.m_handle, -1);
    m_size = other.m_size;
  }
  return *this;
}

RandomAccessFile::OpenResult RandomAccessFile::open(const std::filesystem::path& path) {
  OpenResult result;
  const std::string location = fs_path_to_string(path);
  const int fd = ::open(location.c_str(), O_RDONLY);
  if (fd < 0) {
    result.status = errno_status();
    result.message = location + ": " + std::strerror(errno);
    return result;
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
    ::close(fd);
    result.status = Status::Failed;
    result.message = location + ": not a regular file";
    return result;
  }
  result.status = Status::Ok;
  result.file.m_handle = fd;
  result.file.m_size = static_cast<uint64_t>(st.st_size);
  return result;
}

RandomAccessFile::operator bool() const noexcept {
  return m_handle >= 0;
}

size_t RandomAccessFile::read_at(
    uint64_t offset, std::span<std::byte> out, std::error_code& error) const noexcept {
  error.clear();
  if (m_handle < 0 || out.empty())
    return 0;
  if (::lseek(m_handle, static_cast<off_t>(offset), SEEK_SET) < 0) {
    error.assign(errno, std::generic_category());
    return 0;
  }
  size_t total = 0;
  while (total < out.size()) {
    const ssize_t got =
        ::read(m_handle, out.data() + total, out.size() - total);
    if (got < 0) {
      if (errno == EINTR)
        continue;
      error.assign(errno, std::generic_category());
      break;
    }
    if (got == 0)
      break;
    total += static_cast<size_t>(got);
  }
  return total;
}

bool RandomAccessFile::close() noexcept {
  if (m_handle < 0)
    return true;
  const int fd = std::exchange(m_handle, -1);
  return ::close(fd) == 0;
}

OpenResult open(std::string_view location, File::Mode mode) {
  OpenResult result;
  const std::string path = std::string(location);
  FILE* file = std::fopen(path.c_str(), fopen_mode(mode));
  if (file == nullptr) {
    result.status = errno_status();
    result.message = path + ": " + std::strerror(errno);
    return result;
  }
  result.status = Status::Ok;
  result.file = File(reinterpret_cast<SDL_IOStream*>(file), nullptr,
      mode == File::Mode::Truncate || mode == File::Mode::Append);
  return result;
}

Status check(std::string_view location) {
  struct stat st {};
  if (::stat(std::string(location).c_str(), &st) != 0)
    return errno_status();
  return Status::Ok;
}

std::string display_name(std::string_view location) {
  const std::string path(location);
  const size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos)
    return path;
  return path.substr(slash + 1);
}

bool atomic_replace(const std::filesystem::path& source,
    const std::filesystem::path& destination, std::string& error) {
  if (::rename(fs_path_to_string(source).c_str(),
          fs_path_to_string(destination).c_str()) != 0) {
    error = std::strerror(errno);
    return false;
  }
  return true;
}

JoinResult join(std::string_view folder, std::string_view relativePath) {
  JoinResult result;
  if (relativePath.empty() || relativePath == "." || relativePath == ".." ||
      relativePath.find("..") != std::string_view::npos) {
    result.status = Status::Unsupported;
    result.message = "invalid relative path";
    return result;
  }
  std::error_code ec;
  const std::filesystem::path child =
      to_path(folder) / fs_path_from_utf8(relativePath);
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(child, ec);
  if (ec || !std::filesystem::exists(canonical, ec)) {
    result.status = Status::NotFound;
    result.message = std::string(relativePath);
    return result;
  }
  result.status = Status::Ok;
  result.location = fs_path_to_string(canonical);
  return result;
}

JoinResult create_child(std::string_view folder, std::string_view name) {
  JoinResult result;
  if (name.empty() || name.find_first_of("/\\") != std::string_view::npos) {
    result.status = Status::Unsupported;
    result.message = "invalid child name";
    return result;
  }
  const std::filesystem::path child = to_path(folder) / fs_path_from_utf8(name);
  const std::string path = fs_path_to_string(child);
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0) {
    result.status = errno_status();
    result.message = path + ": " + std::strerror(errno);
    return result;
  }
  ::close(fd);
  result.status = Status::Ok;
  result.location = path;
  return result;
}

ListResult list(std::string_view folder) {
  ListResult result;
  const std::string path = std::string(folder);
  DIR* dir = ::opendir(path.c_str());
  if (dir == nullptr) {
    result.status = errno_status();
    result.message = path + ": " + std::strerror(errno);
    return result;
  }
  while (dirent* entry = ::readdir(dir)) {
    const std::string_view name(entry->d_name);
    if (name == "." || name == "..")
      continue;
    Entry out;
    out.name = std::string(name);
    out.location = path + "/" + out.name;
    struct stat st {};
    out.isDirectory =
        ::stat(out.location.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    result.entries.push_back(std::move(out));
  }
  ::closedir(dir);
  result.status = Status::Ok;
  return result;
}

PathAccess::~PathAccess() = default;

PathAccess::PathAccess(PathAccess&& other) noexcept
    : m_path{std::move(other.m_path)}, m_access{other.m_access} {
  other.m_access = nullptr;
}

PathAccess& PathAccess::operator=(PathAccess&& other) noexcept {
  if (this != &other) {
    m_path = std::move(other.m_path);
    m_access = other.m_access;
    other.m_access = nullptr;
  }
  return *this;
}

PathAccess access_path(std::string_view location) {
  return PathAccess(to_path(location), nullptr);
}

}  // namespace borealis::io
