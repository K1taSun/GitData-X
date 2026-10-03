#pragma once

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace gitdatax::testing {

// Tymczasowy katalog usuwany w destruktorze (RAII), żeby testy nie zostawiały śmieci.
class TempDir {
public:
    TempDir() {
        std::random_device device;
        std::mt19937_64 engine(device());
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("gitdatax_test_" + std::to_string(stamp) + "_" + std::to_string(engine()));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        // error_code zamiast wyjątku: destruktor nie powinien rzucać.
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace gitdatax::testing

