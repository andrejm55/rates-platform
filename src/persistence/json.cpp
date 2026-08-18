#include "rates/persistence/json.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace rates::json {

bool Value::is_null() const { return std::holds_alternative<std::nullptr_t>(storage_); }
bool Value::is_bool() const { return std::holds_alternative<bool>(storage_); }
bool Value::is_number() const { return std::holds_alternative<double>(storage_); }
bool Value::is_string() const { return std::holds_alternative<std::string>(storage_); }
bool Value::is_array() const { return std::holds_alternative<Array>(storage_); }
bool Value::is_object() const { return std::holds_alternative<Object>(storage_); }

bool Value::as_bool() const {
    if (!is_bool()) throw std::runtime_error("JSON value is not boolean");
    return std::get<bool>(storage_);
}

double Value::as_number() const {
    if (!is_number()) throw std::runtime_error("JSON value is not numeric");
    return std::get<double>(storage_);
}

int Value::as_int() const { return static_cast<int>(std::lround(as_number())); }

const std::string& Value::as_string() const {
    if (!is_string()) throw std::runtime_error("JSON value is not a string");
    return std::get<std::string>(storage_);
}

const Value::Array& Value::as_array() const {
    if (!is_array()) throw std::runtime_error("JSON value is not an array");
    return std::get<Array>(storage_);
}

Value::Array& Value::as_array() {
    if (!is_array()) throw std::runtime_error("JSON value is not an array");
    return std::get<Array>(storage_);
}

const Value::Object& Value::as_object() const {
    if (!is_object()) throw std::runtime_error("JSON value is not an object");
    return std::get<Object>(storage_);
}

Value::Object& Value::as_object() {
    if (!is_object()) throw std::runtime_error("JSON value is not an object");
    return std::get<Object>(storage_);
}

bool Value::contains(const std::string& key) const {
    return is_object() && as_object().contains(key);
}

const Value& Value::at(const std::string& key) const {
    const auto& object = as_object();
    auto iterator = object.find(key);
    if (iterator == object.end()) throw std::out_of_range("Missing JSON key: " + key);
    return iterator->second;
}

Value& Value::operator[](const std::string& key) {
    if (is_null()) storage_ = Object{};
    return as_object()[key];
}

const Value& Value::operator[](std::size_t index) const { return as_array().at(index); }

std::string Value::string_or(const std::string& key, const std::string& fallback) const {
    return contains(key) ? at(key).as_string() : fallback;
}
double Value::number_or(const std::string& key, double fallback) const {
    return contains(key) ? at(key).as_number() : fallback;
}
int Value::int_or(const std::string& key, int fallback) const {
    return contains(key) ? at(key).as_int() : fallback;
}
bool Value::bool_or(const std::string& key, bool fallback) const {
    return contains(key) ? at(key).as_bool() : fallback;
}

namespace {
class Parser {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    Value parse_value() {
        skip_whitespace();
        if (position_ >= input_.size()) throw error("Unexpected end of JSON");
        const char token = input_[position_];
        if (token == '{') return parse_object();
        if (token == '[') return parse_array();
        if (token == '"') return Value(parse_string());
        if (token == 't') return parse_literal("true", Value(true));
        if (token == 'f') return parse_literal("false", Value(false));
        if (token == 'n') return parse_literal("null", Value(nullptr));
        if (token == '-' || std::isdigit(static_cast<unsigned char>(token))) return Value(parse_number());
        throw error(std::string("Unexpected token: ") + token);
    }

    void require_end() {
        skip_whitespace();
        if (position_ != input_.size()) throw error("Trailing data after JSON value");
    }

private:
    const std::string& input_;
    std::size_t position_{0};

    std::runtime_error error(const std::string& message) const {
        return std::runtime_error(message + " at JSON position " + std::to_string(position_));
    }

    void skip_whitespace() {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
    }

    void expect(char expected) {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) throw error(std::string("Expected '") + expected + "'");
        ++position_;
    }

    Value parse_object() {
        expect('{');
        Value::Object object;
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == '}') {
            ++position_;
            return object;
        }
        while (true) {
            skip_whitespace();
            if (position_ >= input_.size() || input_[position_] != '"') throw error("Expected object key");
            const std::string key = parse_string();
            expect(':');
            object.emplace(key, parse_value());
            skip_whitespace();
            if (position_ < input_.size() && input_[position_] == '}') {
                ++position_;
                break;
            }
            expect(',');
        }
        return object;
    }

    Value parse_array() {
        expect('[');
        Value::Array array;
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == ']') {
            ++position_;
            return array;
        }
        while (true) {
            array.push_back(parse_value());
            skip_whitespace();
            if (position_ < input_.size() && input_[position_] == ']') {
                ++position_;
                break;
            }
            expect(',');
        }
        return array;
    }

    std::string parse_string() {
        expect('"');
        std::string output;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') return output;
            if (character == '\\') {
                if (position_ >= input_.size()) throw error("Incomplete escape sequence");
                const char escaped = input_[position_++];
                switch (escaped) {
                    case '"': output.push_back('"'); break;
                    case '\\': output.push_back('\\'); break;
                    case '/': output.push_back('/'); break;
                    case 'b': output.push_back('\b'); break;
                    case 'f': output.push_back('\f'); break;
                    case 'n': output.push_back('\n'); break;
                    case 'r': output.push_back('\r'); break;
                    case 't': output.push_back('\t'); break;
                    default: throw error("Unsupported JSON escape sequence");
                }
            } else {
                output.push_back(character);
            }
        }
        throw error("Unterminated JSON string");
    }

    double parse_number() {
        const std::size_t start = position_;
        if (input_[position_] == '-') ++position_;
        while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        try {
            return std::stod(input_.substr(start, position_ - start));
        } catch (...) {
            throw error("Invalid JSON number");
        }
    }

    Value parse_literal(const std::string& literal, Value value) {
        if (input_.substr(position_, literal.size()) != literal) throw error("Invalid JSON literal");
        position_ += literal.size();
        return value;
    }
};

std::string escape_string(const std::string& input) {
    std::ostringstream output;
    for (char character : input) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default: output << character;
        }
    }
    return output.str();
}

void dump_value(std::ostringstream& output, const Value& value, int indent, int depth) {
    const std::string padding(static_cast<std::size_t>(depth * indent), ' ');
    const std::string child_padding(static_cast<std::size_t>((depth + 1) * indent), ' ');
    if (value.is_null()) output << "null";
    else if (value.is_bool()) output << (value.as_bool() ? "true" : "false");
    else if (value.is_number()) output << std::setprecision(15) << value.as_number();
    else if (value.is_string()) output << '"' << escape_string(value.as_string()) << '"';
    else if (value.is_array()) {
        const auto& array = value.as_array();
        output << '[';
        if (!array.empty()) {
            for (std::size_t i = 0; i < array.size(); ++i) {
                if (indent > 0) output << '\n' << child_padding;
                dump_value(output, array[i], indent, depth + 1);
                if (i + 1 < array.size()) output << ',';
            }
            if (indent > 0) output << '\n' << padding;
        }
        output << ']';
    } else {
        const auto& object = value.as_object();
        output << '{';
        if (!object.empty()) {
            std::size_t i = 0;
            for (const auto& [key, child] : object) {
                if (indent > 0) output << '\n' << child_padding;
                output << '"' << escape_string(key) << "\":";
                if (indent > 0) output << ' ';
                dump_value(output, child, indent, depth + 1);
                if (++i < object.size()) output << ',';
            }
            if (indent > 0) output << '\n' << padding;
        }
        output << '}';
    }
}
}

Value parse(const std::string& text) {
    Parser parser(text);
    Value value = parser.parse_value();
    parser.require_end();
    return value;
}

Value parse_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Unable to open JSON file: " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parse(buffer.str());
}

std::string dump(const Value& value, int indent) {
    std::ostringstream output;
    dump_value(output, value, std::max(0, indent), 0);
    return output.str();
}

void write_file(const std::string& path, const Value& value, int indent) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Unable to write JSON file: " + path);
    output << dump(value, indent) << '\n';
}

}  // namespace rates::json
