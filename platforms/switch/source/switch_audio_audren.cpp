#include "switch/audio.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace dusk::sw {

bool audio_init() {
#ifdef __SWITCH__
  AudioRendererConfig cfg{};
  cfg.output_rate = AudioRendererOutputRate_48kHz;
  cfg.num_voices = 24;
  cfg.num_effects = 0;
  cfg.num_sinks = 1;
  cfg.num_mix_objs = 4;
  cfg.num_mix_buffers = 2;
  if (R_FAILED(audrenInitialize(&cfg)))
    return false;
  audrenStartAudioRenderer();
  return true;
#else
  return false;
#endif
}

void audio_exit() {
#ifdef __SWITCH__
  audrenStopAudioRenderer();
  audrenExit();
#endif
}

}
