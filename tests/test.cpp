#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <streambuf>
#include <string>

using namespace std::string_literals;
namespace fs = std::filesystem;
fs::path current_file_path(__FILE__);
fs::path current_directory = current_file_path.remove_filename();
fs::path working_directory = current_directory.parent_path().parent_path();
// helper function for wirte content to file
void write_file(const fs::path &path, const std::string &content) { std::ofstream(path) << content; }

// Get content from file
std::string get_file_contents(const fs::path &path) {
    std::ifstream file(path);
    std::string content(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>{});
    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    return content;
}

std::string run_refactor_tool(const fs::path &path) {
    std::string cmd = working_directory.string() + "/build/refactor_tool "s + path.string() + " --"s;
    std::cout << cmd.c_str() << std::endl;
    int rc = system(cmd.c_str());
    EXPECT_EQ(rc, 0) << "failed to run refactor_tool";

    auto content = get_file_contents(path);
    fs::remove(path);

    return content;
}

// Get refactored content
std::string get_refactored_contents(const std::string &content) {
    fs::path relative_path("tests_data/tmp/test.cpp"s);
    std::filesystem::path tmp_file = current_directory / relative_path;
    write_file(tmp_file, content);
    return run_refactor_tool(tmp_file);
}

TEST(test, nv_dtor1) {
    const auto input = "struct Base { ~Base(); }; "
                       "struct Derived : Base { ~Derived(); };"s;
    const auto expected = "struct Base { virtual ~Base(); }; "
                          "struct Derived : Base { ~Derived(); };"s;
    const auto actual = get_refactored_contents(input);
    EXPECT_EQ(actual, expected);
}

TEST(test, nv_dtor2) {
    const auto input = "struct Base { virtual ~Base(); }; "
                       "struct Derived : Base { ~Derived(); };"s;
    const auto expected = "struct Base { virtual ~Base(); }; "
                          "struct Derived : Base { ~Derived(); };"s;
    const auto actual = get_refactored_contents(input);
    EXPECT_EQ(actual, expected);
}

TEST(test, miss_override1) {
    const auto input = "struct Base { "
                       "  virtual ~Base(); "
                       "  virtual void foo();"
                       "}; "
                       "struct Derived : Base { "
                       "  ~Derived(); "
                       "  void foo();"
                       "};"s;
    const auto expected = "struct Base { "
                          "  virtual ~Base(); "
                          "  virtual void foo();"
                          "}; "
                          "struct Derived : Base { "
                          "  ~Derived(); "
                          "  void foo() override;"
                          "};"s;
    const auto actual = get_refactored_contents(input);
    EXPECT_EQ(actual, expected);
}

TEST(test, miss_override2) {
    const auto input = "struct Base { virtual ~Base(); }; "
                       "struct Derived : Base { ~Derived(); };"s;
    const auto expected = "struct Base { virtual ~Base(); }; "
                          "struct Derived : Base { ~Derived(); };"s;
    const auto actual = get_refactored_contents(input);
    EXPECT_EQ(actual, expected);
}

TEST(test, crange_for1) {
    const auto input = "void f() { "
                       "  struct my {int i; double d;}; "
                       "  my arr[100]; "
                       "  for (const auto ele : arr) {} "
                       "}"s;
    const auto expected = "void f() { "
                          "  struct my {int i; double d;}; "
                          "  my arr[100]; "
                          "  for (const auto& ele : arr) {} "
                          "}"s;
    const auto actual = get_refactored_contents(input);
    EXPECT_EQ(actual, expected);
}

TEST(test, crange_for2) {
    const auto input = "void f() { "
                       "  struct my {int i; double d;}; "
                       "  my arr[100]; "
                       "  for (const auto& ele : arr) {} "
                       "}"s;
    const auto expected = "void f() { "
                          "  struct my {int i; double d;}; "
                          "  my arr[100]; "
                          "  for (const auto& ele : arr) {} "
                          "}"s;
    const auto actual = get_refactored_contents(input);
    EXPECT_EQ(actual, expected);
}

TEST(test, crange_for3) {
    const auto input = "void f() { "
                       "  char arr[100]; "
                       "  for (const char ele : arr) {} "
                       "}"s;
    const auto expected = "void f() { "
                          "  char arr[100]; "
                          "  for (const char ele : arr) {} "
                          "}"s;
    const auto actual = get_refactored_contents(input);
    EXPECT_EQ(actual, expected);
}