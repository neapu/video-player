#pragma once
#include <QString>
#include <vector>

namespace view {
// 可用于解码的 GPU 信息
struct GpuInfo {
    int index{-1};        // GPU 序号，与 media 硬解码的 decodeAdapterIndex 同一序号空间
    QString name{};       // 适配器描述
};

/**
 * @brief 枚举可用于解码的 GPU（DXGI 非软件适配器，按枚举顺序连续编号）；非 Windows 返回空。
 * @note 序号空间约定：与 media::D3d11vaDecoder 内部按序号选择适配器的枚举规则一致
 *       （同为"跳过 DXGI_ADAPTER_FLAG_SOFTWARE 后按 EnumAdapters1 顺序连续编号"），
 *       两处实现必须保持同步修改。
 */
std::vector<GpuInfo> enumerateGpus();

/**
 * @brief 返回 D3D11 设备所在 GPU 的序号（enumerateGpus() 列表内）。
 * @param d3d11Device ID3D11Device 裸指针，只借用。
 * @return 序号；设备为空、枚举失败或非 Windows 返回 -1。
 */
int gpuIndexOfDevice(void* d3d11Device);
} // namespace view
