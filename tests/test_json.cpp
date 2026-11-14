// Small JSON reader used for installations and components.

#include <catch2/catch_all.hpp>

#include <string>

#include "json.h"

namespace {

struct Doc {
  JsonValue* root = nullptr;
  char error[200] = "";
  explicit Doc(const char* text) { root = json_parse(text, error, sizeof(error)); }
  ~Doc() { json_free(root); }
};

}  // namespace

TEST_CASE("JSON parses objects, arrays and scalars", "[json]") {
  Doc d(R"({"id": "demo", "n": -12.5e1, "ok": true, "none": null,
            "list": [1, "two", false, {"x": 0.25}], "empty": {}, "nothing": []})");
  REQUIRE(d.root != nullptr);
  CHECK(json_size(d.root) == 7);
  CHECK(std::string(json_get_string(d.root, "id", "")) == "demo");
  CHECK(json_get_number(d.root, "n", 0) == -125.0);
  CHECK(json_get_bool(d.root, "ok", false));
  CHECK(json_get(d.root, "none")->type == JSON_NULL);
  const JsonValue* list = json_get(d.root, "list");
  REQUIRE(json_size(list) == 4);
  CHECK(json_at(list, 0)->number == 1.0);
  CHECK(std::string(json_at(list, 1)->string) == "two");
  CHECK(json_get_number(json_at(list, 3), "x", 0) == 0.25);
  CHECK(json_at(list, 4) == nullptr);
  CHECK(json_size(json_get(d.root, "empty")) == 0);
  CHECK(json_get(d.root, "missing") == nullptr);
  CHECK(std::string(json_get_string(d.root, "n", "fallback")) == "fallback");  // wrong type
}

TEST_CASE("JSON decodes escapes and UTF-8", "[json]") {
  Doc d(R"(["a\"b\\c\/\n", "\u00B0C", "\uD83D\uDE00", "Ω"])");
  REQUIRE(d.root != nullptr);
  CHECK(std::string(json_at(d.root, 0)->string) == "a\"b\\c/\n");
  CHECK(std::string(json_at(d.root, 1)->string) == "\xC2\xB0" "C");
  CHECK(std::string(json_at(d.root, 2)->string) == "\xF0\x9F\x98\x80");
  CHECK(std::string(json_at(d.root, 3)->string) == "\xCE\xA9");
}

TEST_CASE("JSON reports errors with line and column", "[json]") {
  const char* invalid[] = {
      "",          "{",           "[1, 2",    "{\"a\" 1}", "{\"a\": 1,}", "[01]",   "[1.]",
      "[1e]",      "tru",         "\"open",   "[\"\\x\"]", "[\"\\uD800\"]", "{} x", "{a: 1}",
  };
  for (const char* text : invalid) {
    Doc d(text);
    INFO(text);
    CHECK(d.root == nullptr);
    CHECK(std::string(d.error).rfind("line ", 0) == 0);
  }
  Doc multi("{\n  \"a\": 1,\n  \"b\": ?\n}");
  CHECK(multi.root == nullptr);
  CHECK(std::string(multi.error).rfind("line 3, column 8", 0) == 0);

  std::string deep(70, '[');
  deep += std::string(70, ']');
  Doc d(deep.c_str());
  CHECK(d.root == nullptr);
}
