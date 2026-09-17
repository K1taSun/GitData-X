cmake_minimum_required(VERSION 3.21)
project(gitdatax VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Zależności instaluje vcpkg w trybie manifestu (vcpkg.json).
find_package(unofficial-git2 CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_package(RapidCheck CONFIG REQUIRED)

# Jedna funkcja zamiast powtarzania flag w każdym celu (DRY).
function(gitdatax_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# --- gitdatax_core: logika + warstwa libgit2, bez FTXUI (szybkie testy bez terminala) ---
add_library(gitdatax_core STATIC
    src/core/version.cpp
    src/core/dag_snapshot.cpp
    src/core/time_strategies.cpp
    src/core/commit_splitter.cpp
    src/core/rewrite_planner.cpp
    src/core/rewrite_engine.cpp
    src/core/dag_verifier.cpp
    src/core/backup_manager.cpp
    src/git/repo.cpp
    src/git/commit.cpp
    src/git/in_memory_splitter.cpp
    src/git/transaction.cpp
)
target_include_directories(gitdatax_core PUBLIC src)
target_link_libraries(gitdatax_core PUBLIC unofficial::git2::libgit2package)
gitdatax_enable_warnings(gitdatax_core)

# --- gitdatax: plik wykonywalny z interfejsem TUI ---
add_executable(gitdatax
    src/main.cpp
    src/tui/theme_2026.cpp
    src/tui/dag_canvas.cpp
    src/tui/date_modals.cpp
    src/tui/split_wizard.cpp
    src/tui/app_controller.cpp
)
target_link_libraries(gitdatax PRIVATE
    gitdatax_core
    ftxui::screen ftxui::dom ftxui::component
)
gitdatax_enable_warnings(gitdatax)

# --- gitdatax_tests: Catch2 + RapidCheck, linkuje tylko rdzeń ---
enable_testing()
add_executable(gitdatax_tests
    tests/version_test.cpp
)
target_link_libraries(gitdatax_tests PRIVATE
    gitdatax_core
    Catch2::Catch2WithMain
    rapidcheck
)
gitdatax_enable_warnings(gitdatax_tests)

include(Catch)
catch_discover_tests(gitdatax_tests)

