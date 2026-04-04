#pragma once

#include <string>

namespace raptor::ui {

void initialize_logging(const std::string& service_name, const std::string& configured_level);

}  // namespace raptor::ui