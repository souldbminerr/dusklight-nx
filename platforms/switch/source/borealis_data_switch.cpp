#include <borealis/data.hpp>

#include <borealis/io.hpp>

namespace borealis::data {
namespace detail {

struct ManagerBackend {
  Paths paths;
  bool has_override = false;
};

}  // namespace detail

namespace {

constexpr char kUserPath[] = "sdmc:/switch/dusklight";

void ensure_dir(const std::filesystem::path& path, Status& status) {
  if (status.code != ErrorCode::None)
    return;
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    status.code = ErrorCode::CreateDirectoryFailed;
    status.path = path;
    status.systemError = ec;
  }
}

Status apply_paths(detail::ManagerBackend& backend, std::filesystem::path user) {
  Status status;
  ensure_dir(user, status);
  const std::filesystem::path cache = user / "cache";
  ensure_dir(cache, status);
  if (status.code == ErrorCode::None)
    backend.paths = Paths{user, cache};
  return status;
}

}  // namespace

Manager::Manager(AppInfo, Options)
    : backend_{std::make_unique<detail::ManagerBackend>()} {}

Manager::~Manager() = default;

Manager::Manager(Manager&&) noexcept = default;
Manager& Manager::operator=(Manager&&) noexcept = default;

Status Manager::initialize(const std::filesystem::path& userDirectoryOverride) {
  backend_->has_override = !userDirectoryOverride.empty();
  return apply_paths(*backend_,
      backend_->has_override ? userDirectoryOverride
                             : std::filesystem::path(kUserPath));
}

const Paths& Manager::paths() const noexcept {
  return backend_->paths;
}

const std::filesystem::path& Manager::active_data_path() const noexcept {
  return backend_->paths.userPath;
}

const std::filesystem::path& Manager::configured_data_path() const noexcept {
  return backend_->paths.userPath;
}

LocationMode Manager::configured_mode() const noexcept {
  return backend_->has_override ? LocationMode::Custom : LocationMode::Default;
}

bool Manager::has_user_directory_override() const noexcept {
  return backend_->has_override;
}

bool Manager::is_default_data_path() const {
  return !backend_->has_override;
}

bool Manager::is_data_path_restart_pending() const {
  return false;
}

Capabilities Manager::capabilities() const noexcept {
  return Capabilities{};
}

Status Manager::set_custom_data_path(const std::filesystem::path& path) {
  if (path.empty()) {
    Status status;
    status.code = ErrorCode::EmptyPath;
    return status;
  }
  backend_->has_override = true;
  return apply_paths(*backend_, path);
}

Status Manager::set_portable_data_path() {
  Status status;
  status.code = ErrorCode::Unsupported;
  return status;
}

Status Manager::reset_data_path() {
  backend_->has_override = false;
  return apply_paths(*backend_, std::filesystem::path(kUserPath));
}

Status Manager::open_active_data_path() const {
  Status status;
  status.code = ErrorCode::Unsupported;
  return status;
}

Status Manager::open_folder(const std::filesystem::path&) const {
  Status status;
  status.code = ErrorCode::Unsupported;
  return status;
}

std::filesystem::path Manager::base_path_relative(
    const std::filesystem::path& path) const {
  return std::filesystem::path("romfs:/res") / path;
}

std::filesystem::path Manager::user_home_path() const {
  return std::filesystem::path("sdmc:/");
}

std::filesystem::path Manager::normalized_display_path(
    const std::filesystem::path& path) const {
  return path;
}

std::string Manager::abbreviated_path_string(
    const std::filesystem::path& path) const {
  return io::fs_path_to_string(path);
}

}  // namespace borealis::data
