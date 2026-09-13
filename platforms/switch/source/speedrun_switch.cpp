#include "dusk/livesplit.h"

namespace dusk::speedrun {

void onGameFrame() {}
uint64_t getFrameCount() {
  return 0;
}
void start() {}
void reset() {}
void connectLiveSplit(const char*, int) {}
void disconnectLiveSplit() {}
bool consumeConnectedEvent() {
  return false;
}
bool consumeDisconnectedEvent() {
  return false;
}
void updateLiveSplit() {}
void shutdown() {}

}  // namespace dusk::speedrun
