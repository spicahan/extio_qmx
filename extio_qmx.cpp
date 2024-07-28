#include <portaudio.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

#include <windows.h>

// Forward declarations
// Refer to https://www.i2phd.org/code/Winrad_Extio.pdf for API definitions
extern "C" {
    bool __stdcall InitHW(char *name, char *model, int& hwType);
    bool __stdcall OpenHW();
    void __stdcall CloseHW();
    int __stdcall StartHW(long freq);
    void __stdcall StopHW();
    long __stdcall GetHWLO();
    int __stdcall SetHWLO(long LOfreq);
    long __stdcall GetHWSR();
    int __stdcall GetStatus();
    void __stdcall SetCallback(void (*Callback)(int, int, float, void*));
}

constexpr long SampleRate = 48000;
constexpr int IQPairs = 512;
constexpr long qmxIFFreq = 12000;
constexpr long qmxSidetoneFreq = 700;

// Global variables
PaDeviceIndex deviceIdx = paNoDevice;
PaStream *stream = nullptr;
void (*IQCallback)(int, int, float, void*) = nullptr;
bool started = false;
static long fakeLOFreq = 0;

// Function to enumerate and initialize the soundcard
PaDeviceIndex FindSoundCard() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return paNoDevice;
    }

    PaDeviceIndex numDevices = Pa_GetDeviceCount();

    for (PaDeviceIndex i = 0; i < numDevices; ++i) {
        const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(i);
        std::string deviceName = deviceInfo->name;
        if (deviceName.find("QMX") != std::string::npos || deviceName.find("QDX") != std::string::npos) {
            // inputLatency = deviceInfo->defaultLowInputLatency;
            return i;
        }
    }

    std::cerr << "No suitable soundcard found." << std::endl;
    return paNoDevice;
}

// Callback function for PortAudio
static int paCallback(const void *input, void *output,
                      unsigned long frameCount,
                      const PaStreamCallbackTimeInfo *timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData) {
    std::cout << "paCallback() called" << std::endl;
    assert(frameCount == IQPairs);

    if (started && IQCallback) {
        auto inputPtr = reinterpret_cast<intptr_t>(input);
        IQCallback(frameCount, 0, 0.0f, reinterpret_cast<void *>(inputPtr));
    }

    return paContinue;
}

// API implementations
bool __stdcall InitHW(char *name, char *model, int& hwType) {
    deviceIdx = FindSoundCard();
    if (deviceIdx == paNoDevice) {
        std::cerr << "No devices available to open." << std::endl;
        return false;
    }
    strcpy(name, "SDR for QRP Labs QDX/QMX");
    strcpy(model, "QDX/QMX");
    hwType = 7; // Float32
    return true;
}

bool __stdcall OpenHW() {
    assert(deviceIdx != paNoDevice);

    PaStreamParameters inputParameters;
    inputParameters.device = deviceIdx;
    inputParameters.channelCount = 2;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = 0;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&stream, &inputParameters, nullptr, double(SampleRate), IQPairs, paClipOff, paCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    return true;
}

void __stdcall CloseHW() {
    started = false;
    if (stream) {
        Pa_CloseStream(stream);
        stream = nullptr;
    }
    Pa_Terminate();
}

void __stdcall SetCallback(void (*Callback)(int, int, float, void*)) {
    IQCallback = Callback;
}

// Other empty APIs
int __stdcall StartHW(long freq) {
    started = true;
    PaError err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        started = false;
        return 0;
    }
    fakeLOFreq = freq;
    return IQPairs;
}

void __stdcall StopHW() {
    started = false;
    PaError err = Pa_StopStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
    }
}

long __stdcall GetHWLO() {
    return fakeLOFreq - (qmxIFFreq + qmxSidetoneFreq);
}

int __stdcall SetHWLO(long LOfreq) {
    fakeLOFreq = LOfreq;
    return 0;
}

long __stdcall GetHWSR() {
    return SampleRate;
    
}

int __stdcall GetStatus() {
    return 0;
}

static void test_cb(int cnt, int status, float offset, void* buffer) {
    std::cout << "test_cb() called with cnt = " << cnt << std::endl;
    float (*iq_buffer)[2] = reinterpret_cast<float (*)[2]>(buffer);
    for (int i = 0; i < cnt; i++) {
        std::cout << "I: " << iq_buffer[i][0] << ", Q: " << iq_buffer[i][1] << std::endl;
    }
}

int main() {
    char sdrName[256];
    char sdrModel[256];
    int sdrType = 0;
    bool rc = InitHW(sdrName, sdrModel, sdrType);
    rc = OpenHW();
    std::cout << "OpenHW() result: " << rc << std::endl;
    SetCallback(test_cb);
    int pairs = StartHW(14050000);
    std::cout << "StartHW() result: " << pairs << std::endl;
    Sleep(1000);
    StopHW();
    CloseHW();
    return 0;
}
