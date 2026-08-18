#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace rates::json {

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Value() : storage_(nullptr) {}
    Value(std::nullptr_t) : storage_(nullptr) {}
    Value(bool value) : storage_(value) {}
    Value(int value) : storage_(static_cast<double>(value)) {}
    Value(double value) : storage_(value) {}
    Value(std::string value) : storage_(std::move(value)) {}
    Value(const char* value) : storage_(std::string(value)) {}
    Value(Array value) : storage_(std::move(value)) {}
    Value(Object value) : storage_(std::move(value)) {}

    [[nodiscard]] bool is_null() const;
    [[nodiscard]] bool is_bool() const;
    [[nodiscard]] bool is_number() const;
    [[nodiscard]] bool is_string() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] bool is_object() const;

    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] double as_number() const;
    [[nodiscard]] int as_int() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const Array& as_array() const;
    [[nodiscard]] Array& as_array();
    [[nodiscard]] const Object& as_object() const;
    [[nodiscard]] Object& as_object();

    [[nodiscard]] bool contains(const std::string& key) const;
    [[nodiscard]] const Value& at(const std::string& key) const;
    [[nodiscard]] Value& operator[](const std::string& key);
    [[nodiscard]] const Value& operator[](std::size_t index) const;

    [[nodiscard]] std::string string_or(const std::string& key, const std::string& fallback) const;
    [[nodiscard]] double number_or(const std::string& key, double fallback) const;
    [[nodiscard]] int int_or(const std::string& key, int fallback) const;
    [[nodiscard]] bool bool_or(const std::string& key, bool fallback) const;

private:
    Storage storage_;
};

[[nodiscard]] Value parse(const std::string& text);
[[nodiscard]] Value parse_file(const std::string& path);
[[nodiscard]] std::string dump(const Value& value, int indent = 2);
void write_file(const std::string& path, const Value& value, int indent = 2);

}  // namespace rates::json
