#include "rates/persistence/json.hpp"
#include "rates/services/application_service.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(rates_engine_py, module) {
    module.doc() = "Optional Python bindings for the C++ rates derivatives application service";
    module.def("execute_json", [](const std::string& input) {
        const auto request = rates::json::parse(input);
        return rates::json::dump(rates::ApplicationService::execute(request), 2);
    });
}
