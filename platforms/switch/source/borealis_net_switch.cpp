#include <borealis/net.hpp>

namespace borealis::net {

struct Context::Impl {};

Context::Context(ContextOptions) : m_impl{std::make_unique<Impl>()} {}
Context::~Context() = default;

SocketId Context::connect(std::string_view, StreamOptions, void*) {
  return 0;
}

Context::BindResult Context::listen(std::string_view, ListenOptions, void*) {
  return BindResult{.id = 0, .error = Error::Network, .message = "network unavailable on Switch"};
}

Context::BindResult Context::open_datagram(std::string_view, DatagramOptions, void*) {
  return BindResult{.id = 0, .error = Error::Network, .message = "network unavailable on Switch"};
}

SocketId Context::resolve(std::string_view, void*) {
  return 0;
}

SendResult Context::send(SocketId, std::span<const std::byte>) {
  return SendResult::NotOpen;
}

SendResult Context::send_to(SocketId, std::string_view, std::span<const std::byte>) {
  return SendResult::NotOpen;
}

void Context::set_user_data(SocketId, void*) {}

std::optional<Stats> Context::stats(SocketId) const {
  return std::nullopt;
}

void Context::close(SocketId) {}

bool Context::poll(Event&) {
  return false;
}

bool available() noexcept {
  return false;
}

std::optional<ParsedEndpoint> parse_endpoint(std::string_view) {
  return std::nullopt;
}

std::string format_endpoint(const ParsedEndpoint& endpoint) {
  std::string out = endpoint.scheme;
  out += "://";
  if (endpoint.ipv6) {
    out += '[';
  }
  out += endpoint.host;
  if (endpoint.ipv6) {
    out += ']';
  }
  out += ':';
  out += std::to_string(endpoint.port);
  return out;
}

}  // namespace borealis::net
