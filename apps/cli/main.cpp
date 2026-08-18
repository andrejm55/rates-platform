#include "rates/persistence/json.hpp"
#include "rates/services/application_service.hpp"
#include <iostream>
#include <string>

namespace {
void print_usage() {
    std::cout << "Rates Derivatives Engine CLI\n\n"
              << "Usage:\n"
              << "  rates_cli --input <request.json> [--output <result.json>]\n"
              << "  rates_cli --version\n\n"
              << "The input JSON must contain top-level 'market' and 'request' objects.\n";
}
}

int main(int argc, char** argv) {
    std::string input_path;
    std::string output_path;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--input" && index + 1 < argc) input_path = argv[++index];
        else if (argument == "--output" && index + 1 < argc) output_path = argv[++index];
        else if (argument == "--version") {
            std::cout << "rates-derivatives-engine 0.1.0\n";
            return 0;
        } else if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << "\n";
            print_usage();
            return 2;
        }
    }
    if (input_path.empty()) {
        print_usage();
        return 2;
    }

    try {
        const auto input = rates::json::parse_file(input_path);
        const auto output = rates::ApplicationService::execute(input);
        if (output_path.empty()) std::cout << rates::json::dump(output, 2) << '\n';
        else rates::json::write_file(output_path, output, 2);
        return 0;
    } catch (const std::exception& error) {
        rates::json::Value output(rates::json::Value::Object{});
        output["success"] = false;
        output["error"] = error.what();
        if (output_path.empty()) std::cout << rates::json::dump(output, 2) << '\n';
        else rates::json::write_file(output_path, output, 2);
        return 1;
    }
}
