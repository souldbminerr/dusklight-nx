#include <borealis/ws.hpp>

namespace borealis::ws {
namespace detail {

struct ConnectionState {};

class ConnectionAccess {
public:
  static Connection make() {
    return Connection(std::shared_ptr<ConnectionState>{});
  }
};

}  // namespace detail

Connection::Connection(Connection&& other) noexcept = default;
Connection& Connection::operator=(Connection&& other) noexcept = default;
Connection::~Connection() = default;

Connection::Connection(std::shared_ptr<detail::ConnectionState> state)
    : m_state{std::move(state)} {}

void Connection::reset() noexcept {
  m_state.reset();
}

Connection::operator bool() const noexcept {
  return m_state != nullptr;
}

Connection::State Connection::state() const noexcept {
  return State::Closed;
}

bool Connection::poll(Event&) {
  return false;
}

SendResult Connection::send(MessageKind, std::string_view) {
  return SendResult::NotOpen;
}

CloseResult Connection::close(uint16_t, std::string_view) {
  return CloseResult::NotOpen;
}

bool available() noexcept {
  return false;
}

Connection connect(Options) {
  return detail::ConnectionAccess::make();
}

}  // namespace borealis::ws
