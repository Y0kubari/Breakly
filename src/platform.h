#pragma once
#include "core.h"
#include <windows.h>
#include <string>
#include <filesystem>

namespace breakly {
class ActivityMonitor final : public IActivity { public: Ms idle() override; };
class SystemClock final : public IClock {
public:
    ActivityMonitor activity;
    bool locked=false, suspended=false;
    Sample sample() override;
};
class AutostartManager {
public:
    static bool enabled();
    static bool set(bool);
};
class BreakPolicyEngine {
public: static bool fullscreen();
};
std::filesystem::path dataDirectory();
std::wstring executablePath();
bool isWindowsBuild(DWORD);
bool systemDark();
bool highContrast();
bool transparency();
void decorate(HWND,bool dark,bool mica,bool effects);
HICON makeIcon(int size);
std::wstring durationText(Ms);
std::wstring kindText(Kind);
}
