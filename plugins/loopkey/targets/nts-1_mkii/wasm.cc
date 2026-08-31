// reference: https://emscripten.org/docs/api_reference/wasm_audio_worklets.html#wasm-audio-worklets

#include <emscripten/bind.h>
#include <emscripten/webaudio.h>
#include <emscripten/em_math.h>
using namespace emscripten;
#include "unit_osc.h"
#include "loopkey.h"

uint8_t audioThreadStack[4096];

constexpr int SAMPLE_RATE = 48000;
constexpr int WEB_AUDIO_FRAME_SIZE = 128;
std::vector<float> ram;
std::array<float, WEB_AUDIO_FRAME_SIZE * 2> interleavedIn;
std::array<float, WEB_AUDIO_FRAME_SIZE> interleavedOut;

LoopKey processor;
extern const unit_header_t unit_header;

static float BPM_WASM = 120.f;
static float test_phase_ = 0.f;

void fx_set_bpm(float bpm)
{
  BPM_WASM = bpm;
  processor.setTempo(bpm);
}

uint16_t fx_get_bpm(void)
{
  return static_cast<int>(BPM_WASM * 10.f);
}

float fx_get_bpmf(void)
{
  return BPM_WASM;
}

struct AudioWorkletParameter
{
  int min;
  int max;
  int center;
  int init;
  uint8_t type;
  std::string name;
};

std::string getParameterValueString(int index, int value)
{
  const unit_param_t &p = unit_header.params[index];

  std::string suffix;

  switch (p.type)
  {
  case k_unit_param_type_none:
    break;
  case k_unit_param_type_percent:
    suffix = "%";
    break;
  case k_unit_param_type_strings:
    return processor.getParameterStrValue(index, value);
  default:
    return "unimplemented";
  };

  std::string numerical;
  if (p.frac_mode == k_unit_param_frac_mode_fixed)
  {
    numerical = std::to_string(value / static_cast<double>(1 << p.frac));
  }
  else
  {
    numerical = std::to_string(value / std::pow(10.0, p.frac));
  }
  numerical.erase(numerical.find_last_not_of('0') + 1);
  if (!numerical.empty() && numerical.back() == '.')
  {
    numerical.pop_back();
  }

  return numerical + suffix;
}

std::vector<AudioWorkletParameter> getValidParameters()
{
  std::vector<AudioWorkletParameter> result;
  for (int paramIndex = 0; paramIndex < unit_header.num_params; ++paramIndex)
  {
    const unit_param_t &p = unit_header.params[paramIndex];
    result.push_back({p.min,
                      p.max,
                      p.center,
                      p.init,
                      p.type,
                      std::string(p.name)});
  }
  return result;
}

void setOscPitch(float f0)
{
  processor.setPitch(f0 / static_cast<float>(SAMPLE_RATE));
}

void noteOn(uint8_t note, uint8_t velocity)
{
  processor.noteOn(note, velocity);
}

void noteOff(uint8_t note)
{
  processor.noteOff(note);
}

EMSCRIPTEN_BINDINGS(my_module)
{
  value_object<AudioWorkletParameter>("AudioWorkletParameter")
      .field("min", &AudioWorkletParameter::min)
      .field("max", &AudioWorkletParameter::max)
      .field("center", &AudioWorkletParameter::center)
      .field("init", &AudioWorkletParameter::init)
      .field("type", &AudioWorkletParameter::type)
      .field("name", &AudioWorkletParameter::name);

  register_vector<AudioWorkletParameter>("ParameterList");

  function("getValidParameters", &getValidParameters);
  function("getParameterValueString", &getParameterValueString);
  function("fx_set_bpm", &fx_set_bpm);
  function("setOscPitch", &setOscPitch);
  function("noteOn", &noteOn);
  function("noteOff", &noteOff);
}

bool ProcessAudio(int numInputs, const AudioSampleFrame *inputs,
                  int numOutputs, AudioSampleFrame *outputs,
                  int numParams, const AudioParamFrame *params,
                  void *userData)
{
  assert(numOutputs == 1);
  assert(outputs->numberOfChannels == 1);
  assert(outputs->samplesPerChannel == WEB_AUDIO_FRAME_SIZE);
  auto &output = outputs[0];

  for (int paramIndex = 0; paramIndex < numParams; ++paramIndex)
  {
    float value = params[paramIndex].data[0];
    processor.setParameter(paramIndex, value);
  }

  for (int sampleIndex = 0; sampleIndex < WEB_AUDIO_FRAME_SIZE; ++sampleIndex)
  {
    float input_sample = 0.f;
    if (numInputs > 0 && inputs[0].data)
    {
      input_sample = inputs[0].data[sampleIndex];
      if (inputs[0].numberOfChannels > 1)
        input_sample = (input_sample + inputs[0].data[sampleIndex + WEB_AUDIO_FRAME_SIZE]) * 0.5f;
    }
    else
    {
      test_phase_ += 440.f * 2.f * 3.14159265f / static_cast<float>(SAMPLE_RATE);
      if (test_phase_ > 2.f * 3.14159265f)
        test_phase_ -= 2.f * 3.14159265f;
      input_sample = sinf(test_phase_) * 0.25f;
    }
    interleavedIn[2 * sampleIndex] = input_sample;
    interleavedIn[2 * sampleIndex + 1] = input_sample;
  }

  processor.process(interleavedIn.data(), interleavedOut.data(), WEB_AUDIO_FRAME_SIZE);

  for (int sampleIndex = 0; sampleIndex < WEB_AUDIO_FRAME_SIZE; ++sampleIndex)
  {
    output.data[sampleIndex] = interleavedOut[sampleIndex];
  }
  return true;
}

void AudioWorkletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T audioContext, bool success, void *userData)
{
  if (!success)
    return;

  ram.resize(processor.getBufferSize());
  processor.init(ram.data());

  // osc.html does not connect a source; ProcessAudio synthesizes a test tone.
  int outputChannelCounts[1] = {1};
  EmscriptenAudioWorkletNodeCreateOptions options = {
      .numberOfInputs = 0,
      .numberOfOutputs = 1,
      .outputChannelCounts = outputChannelCounts};

  EMSCRIPTEN_AUDIO_WORKLET_NODE_T wasmAudioWorklet = emscripten_create_wasm_audio_worklet_node(audioContext,
                                                                                               "logue-osc", &options, &ProcessAudio, 0);

  EM_ASM({
    var ready = (typeof Module !== "undefined" && Module.onAudioReady) ? Module.onAudioReady : setupWebAudioAndUI;
    ready(emscriptenGetAudioObject($0), emscriptenGetAudioObject($1));
  }, audioContext, wasmAudioWorklet);
}

void AudioThreadInitialized(EMSCRIPTEN_WEBAUDIO_T audioContext, bool success, void *userData)
{
  if (!success)
    return;

  auto valid_parameters = getValidParameters();

  WebAudioParamDescriptor params[valid_parameters.size()];
  for (int paramIndex = 0; paramIndex < valid_parameters.size(); ++paramIndex)
  {
    params[paramIndex].automationRate = WEBAUDIO_PARAM_K_RATE;
    params[paramIndex].defaultValue = valid_parameters[paramIndex].init;
    params[paramIndex].minValue = valid_parameters[paramIndex].min;
    params[paramIndex].maxValue = valid_parameters[paramIndex].max;
  }

  WebAudioWorkletProcessorCreateOptions opts = {
      .name = "logue-osc",
      .numAudioParams = static_cast<int>(valid_parameters.size()),
      .audioParamDescriptors = params};

  emscripten_create_wasm_audio_worklet_processor_async(audioContext, &opts, &AudioWorkletProcessorCreated, 0);
}

int main()
{
  EmscriptenWebAudioCreateAttributes attrs = {
      .latencyHint = "interactive",
      .sampleRate = SAMPLE_RATE};

  EMSCRIPTEN_WEBAUDIO_T context = emscripten_create_audio_context(&attrs);

  emscripten_start_wasm_audio_worklet_thread_async(context, audioThreadStack, sizeof(audioThreadStack),
                                                   &AudioThreadInitialized, 0);

  emscripten_exit_with_live_runtime();
}
