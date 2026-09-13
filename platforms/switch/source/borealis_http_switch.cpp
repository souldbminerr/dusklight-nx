#include <borealis/http.hpp>

namespace borealis::http {

bool available() noexcept {
  return false;
}

Backend backend() noexcept {
  return Backend::None;
}

const char* backend_name() noexcept {
  return "none";
}

Task<Result> start(Request) {
  Result result;
  result.error = Error::NoBackend;
  result.message = "no HTTP backend on Switch";
  return detail::make_ready_task(std::move(result));
}

}  // namespace borealis::http
