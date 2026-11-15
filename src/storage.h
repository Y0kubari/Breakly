#pragma once
#include "core.h"
#include <filesystem>

namespace breakly {
class LoggingService {
public: static void error(const std::filesystem::path&,const char* event) noexcept;
};
class FileStore final : public IStore {
public:
    explicit FileStore(std::filesystem::path root):root_(std::move(root)){}
    bool loadSettings(Settings&) override;
    bool saveSettings(const Settings&) override;
    bool loadStatistics(std::map<int,Daily>&) override;
    bool saveStatistics(const std::map<int,Daily>&) override;
    bool write(const std::wstring& name,const std::string& data);
    std::string read(const std::wstring& name) const;
    const std::filesystem::path& root() const {return root_;}
    bool damaged=false;
    std::wstring error;
private:
    std::filesystem::path root_;
    void preserve(const std::wstring&);
};
}
