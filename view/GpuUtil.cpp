#include "GpuUtil.h"
#include <map>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <wrl/client.h>
#endif

namespace view {
std::vector<GpuInfo> enumerateGpus()
{
    std::vector<GpuInfo> gpus;
#ifdef _WIN32
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return gpus;
    }
    int index = 0;
    for (UINT i = 0;; i++) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        GpuInfo info{};
        info.index = index++;
        info.name = QString::fromWCharArray(desc.Description);
        gpus.push_back(std::move(info));
    }
    // 双卡同型号时两块物理 GPU 的适配器描述完全相同，追加序号便于菜单区分
    std::map<QString, int> counts;
    for (const auto& gpu : gpus) {
        ++counts[gpu.name];
    }
    std::map<QString, int> seq;
    for (auto& gpu : gpus) {
        if (counts[gpu.name] > 1) {
            gpu.name += QString(" (#%1)").arg(++seq[gpu.name]);
        }
    }
#endif
    return gpus;
}

int gpuIndexOfDevice(void* d3d11Device)
{
#ifdef _WIN32
    auto* device = static_cast<ID3D11Device*>(d3d11Device);
    if (!device) {
        return -1;
    }
    // 设备 → 所在适配器 LUID，再与枚举列表匹配
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())))
        || FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))
        || FAILED(adapter->GetDesc(&desc))) {
        return -1;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return -1;
    }
    int index = 0;
    for (UINT i = 0;; i++) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        if (factory->EnumAdapters1(i, candidate.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 cand{};
        candidate->GetDesc1(&cand);
        if (cand.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        if (cand.AdapterLuid.LowPart == desc.AdapterLuid.LowPart
            && cand.AdapterLuid.HighPart == desc.AdapterLuid.HighPart) {
            return index;
        }
        ++index;
    }
#endif
    return -1;
}
} // namespace view
