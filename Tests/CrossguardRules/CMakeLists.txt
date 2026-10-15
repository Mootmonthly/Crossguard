# Standalone build of the engine-free CrossguardRules module, for unit tests.
#   cmake -S Tests/CrossguardRules -B build/rules-tests && cmake --build build/rules-tests && ctest --test-dir build/rules-tests
cmake_minimum_required(VERSION 3.20)
project(CrossguardRulesTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Source/CrossguardRules)
file(GLOB RULES_SOURCES ${RULES_DIR}/Private/*.cpp)
# The module entry point needs Unreal headers.
list(FILTER RULES_SOURCES EXCLUDE REGEX "CrossguardRulesModule\\.cpp$")

add_executable(CrossguardRulesTests ${RULES_SOURCES} TestMain.cpp)
target_include_directories(CrossguardRulesTests PRIVATE ${RULES_DIR}/Public)
# Unreal builds without exceptions or RTTI; catch any use of them here.
target_compile_options(CrossguardRulesTests PRIVATE -Wall -Wextra -Wshadow -Werror -fno-exceptions -fno-rtti)

enable_testing()
add_test(NAME CrossguardRulesTests COMMAND CrossguardRulesTests)
