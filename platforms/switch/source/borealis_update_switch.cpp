#include <borealis/update.hpp>

#include <nlohmann/json.hpp>

#include <charconv>

namespace borealis::update {
namespace {

std::optional<int> parse_int(std::string_view value, size_t& pos) {
  int number = 0;
  const char* first = value.data() + pos;
  const char* last = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, number);
  if (ec != std::errc() || ptr == first)
    return std::nullopt;
  pos = static_cast<size_t>(ptr - value.data());
  return number;
}

}  // namespace

std::optional<Version> parse_version(std::string_view value) {
  size_t pos = 0;
  if (!value.empty() && (value[0] == 'v' || value[0] == 'V'))
    pos = 1;
  Version version;
  auto major = parse_int(value, pos);
  if (!major)
    return std::nullopt;
  version.major = *major;
  if (pos >= value.size() || value[pos] != '.')
    return std::nullopt;
  ++pos;
  auto minor = parse_int(value, pos);
  if (!minor)
    return std::nullopt;
  version.minor = *minor;
  if (pos < value.size() && value[pos] == '.') {
    ++pos;
    auto patch = parse_int(value, pos);
    if (!patch)
      return std::nullopt;
    version.patch = *patch;
  }
  if (pos < value.size() && value[pos] == '-') {
    ++pos;
    const size_t end = value.find_first_of("+-", pos);
    version.prerelease.push_back(
        std::string(value.substr(pos, end - pos)));
    pos = end == std::string_view::npos ? value.size() : end;
  }
  if (pos < value.size() && value[pos] == '+') {
    version.distance = std::string(value.substr(pos + 1));
  }
  return version;
}

int compare_version(const Version& lhs, const Version& rhs) {
  if (lhs.major != rhs.major)
    return lhs.major < rhs.major ? -1 : 1;
  if (lhs.minor != rhs.minor)
    return lhs.minor < rhs.minor ? -1 : 1;
  if (lhs.patch != rhs.patch)
    return lhs.patch < rhs.patch ? -1 : 1;
  if (lhs.prerelease != rhs.prerelease)
    return lhs.prerelease.empty() ? 1 : (rhs.prerelease.empty() ? -1 : -1);
  return 0;
}

Release parse_github_release(std::string_view json) {
  const nlohmann::json root =
      nlohmann::json::parse(json.begin(), json.end());
  Release release;
  release.tagName = root.value("tag_name", "");
  release.name = root.value("name", "");
  release.htmlUrl = root.value("html_url", "");
  release.body = root.value("body", "");
  if (root.contains("assets") && root["assets"].is_array()) {
    for (const auto& asset : root["assets"]) {
      Asset out;
      out.name = asset.value("name", "");
      out.browserDownloadUrl = asset.value("browser_download_url", "");
      out.digest = asset.value("digest", "");
      release.assets.push_back(std::move(out));
    }
  }
  return release;
}

Task<Result> check_latest_github_release(const AppInfo&, const Options&) {
  Result result;
  result.status = Status::Disabled;
  result.message = "no HTTP backend on Switch";
  return detail::make_ready_task(std::move(result));
}

}  // namespace borealis::update
