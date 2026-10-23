#include "InputContext.h"
#include <stdexcept>

namespace fh {
void InputContextDeleter::operator()(AVFormatContext* ctx) const
{
    if (ctx) {
        avformat_close_input(&ctx);
    }
}

std::expected<InputContextPtr, int> makeInputContext(const char* filePath, AVIOInterruptCB interruptCallback)
{
    AVFormatContext* ctx = nullptr;
    if (interruptCallback.callback) {
        ctx = avformat_alloc_context();
        if (!ctx) {
            return std::unexpected(AVERROR(ENOMEM));
        }
        ctx->interrupt_callback = interruptCallback;
    }
    int ret = avformat_open_input(&ctx, filePath, nullptr, nullptr);
    if (ret < 0) {
        return std::unexpected(ret);
    }
    return InputContextPtr(ctx);
}

} // namespace fh