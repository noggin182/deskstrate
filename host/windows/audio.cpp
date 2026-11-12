#include "audio.h"

#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <atomic>

#include "kvm_proto.h"
#include "log.h"

namespace audio {

namespace {

HWND g_hwnd = nullptr;
UINT g_msg = 0;
IMMDeviceEnumerator *g_enum = nullptr;
IAudioEndpointVolume *g_volume = nullptr;  // the current default device's, or null
std::atomic<bool> g_rebind{true};

void notify() {
    if (g_hwnd) PostMessageW(g_hwnd, g_msg, 0, 0);
}

// Both callbacks arrive on Core Audio's own threads: they only post to the window.
class VolumeCallback : public IAudioEndpointVolumeCallback {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }   // static lifetime
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioEndpointVolumeCallback)) {
            *out = static_cast<IAudioEndpointVolumeCallback *>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        notify();
        return S_OK;
    }
};

class DeviceCallback : public IMMNotificationClient {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient *>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) {
            g_rebind = true;
            notify();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        g_rebind = true;
        notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

VolumeCallback g_volume_cb;
DeviceCallback g_device_cb;

void unbind() {
    if (!g_volume) return;
    g_volume->UnregisterControlChangeNotify(&g_volume_cb);
    g_volume->Release();
    g_volume = nullptr;
}

void bind() {
    unbind();
    IMMDevice *device = nullptr;
    if (FAILED(g_enum->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        trace("volume: no output device");
        return;
    }
    IAudioEndpointVolume *volume = nullptr;
    const HRESULT hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void **>(&volume));
    device->Release();
    if (FAILED(hr)) {
        trace("volume: output device has no volume control (0x%08lx)", static_cast<unsigned long>(hr));
        return;
    }
    volume->RegisterControlChangeNotify(&g_volume_cb);
    g_volume = volume;
}

}  // namespace

bool start(HWND hwnd, UINT msg) {
    g_hwnd = hwnd;
    g_msg = msg;
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&g_enum));
    if (FAILED(hr)) {
        trace("volume: Core Audio unavailable (0x%08lx)", static_cast<unsigned long>(hr));
        g_enum = nullptr;
        return false;
    }
    g_enum->RegisterEndpointNotificationCallback(&g_device_cb);
    g_rebind = true;
    notify();
    return true;
}

bool read(int &level, bool &muted) {
    if (!g_enum) return false;
    if (g_rebind.exchange(false)) bind();
    if (!g_volume) return false;
    float scalar = 0;
    BOOL mute = FALSE;
    if (FAILED(g_volume->GetMasterVolumeLevelScalar(&scalar)) || FAILED(g_volume->GetMute(&mute))) {
        g_rebind = true;  // the device went away under us: try again on the next change
        return false;
    }
    level = kvm_level_from_scalar(scalar);
    muted = mute != FALSE;
    return true;
}

void stop() {
    unbind();
    if (g_enum) {
        g_enum->UnregisterEndpointNotificationCallback(&g_device_cb);
        g_enum->Release();
        g_enum = nullptr;
    }
    g_hwnd = nullptr;
}

}  // namespace audio
