#pragma once

#include <string>

#include "raptor_ui/config.hpp"
#include "raptor_ui/display_backend.hpp"
#include "raptor_ui/page_controller.hpp"

namespace raptor::ui {

void apply_page_assignment(UiSnapshot& snapshot, const PageController& page_controller);
UiSnapshot initialize_snapshot(const DisplayConfig& display_config,
                               const DisplayBackend& backend,
                               const PageController& page_controller);
bool activate_boot_presentation(PageController& page_controller, const ServiceConfig& config, std::string& error_message);

}  // namespace raptor::ui
