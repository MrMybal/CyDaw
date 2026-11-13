#pragma once
#include <pa_asio.h>
#include <portaudio.h>
namespace cydaw::platform::detail {
struct AsioApi {
  decltype(&Pa_Initialize) initialize = Pa_Initialize;
  decltype(&Pa_Terminate) terminate = Pa_Terminate;
  decltype(&Pa_HostApiTypeIdToHostApiIndex) hostIndex =
      Pa_HostApiTypeIdToHostApiIndex;
  decltype(&Pa_GetHostApiInfo) hostInfo = Pa_GetHostApiInfo;
  decltype(&Pa_HostApiDeviceIndexToDeviceIndex) deviceIndex =
      Pa_HostApiDeviceIndexToDeviceIndex;
  decltype(&Pa_GetDeviceInfo) deviceInfo = Pa_GetDeviceInfo;
  decltype(&Pa_IsFormatSupported) supports = Pa_IsFormatSupported;
  decltype(&Pa_OpenStream) open = Pa_OpenStream;
  decltype(&Pa_StartStream) start = Pa_StartStream;
  decltype(&Pa_StopStream) stop = Pa_StopStream;
  decltype(&Pa_AbortStream) abort = Pa_AbortStream;
  decltype(&Pa_CloseStream) close = Pa_CloseStream;
  decltype(&Pa_IsStreamActive) active = Pa_IsStreamActive;
  decltype(&Pa_GetStreamInfo) streamInfo = Pa_GetStreamInfo;
  decltype(&Pa_SetStreamFinishedCallback) onFinished =
      Pa_SetStreamFinishedCallback;
  decltype(&PaAsio_GetAvailableBufferSizes) bufferSizes =
      PaAsio_GetAvailableBufferSizes;
  decltype(&PaAsio_GetInputChannelName) inputName = PaAsio_GetInputChannelName;
  decltype(&PaAsio_GetOutputChannelName) outputName =
      PaAsio_GetOutputChannelName;
  decltype(&PaAsio_ShowControlPanel) panel = PaAsio_ShowControlPanel;
};
} // namespace cydaw::platform::detail
