#pragma once
#include <string>

namespace view {
class LogManager {
public:
    static void installMessageHandler();
    static void uninstallMessageHandler();

    static std::string mediaLogPath();
};
} // namespace view