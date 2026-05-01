#include "raptor_ui/config.hpp"
#include "raptor_ui/display_backend.hpp"
#include "raptor_ui/logger.hpp"
#include "raptor_ui/page_controller.hpp"
#include "raptor_ui/ui_runtime.hpp"

#include <exception>
#include <string>

#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
    try {
        const std::string config_path = argc > 1
            ? argv[1]
            : "/etc/raptor-native-ui/raptor-native-ui.yaml";

        auto config = raptor::ui::load_config(config_path);
        raptor::ui::initialize_logging("raptor-native-splash-screen", config.logging.level);
        spdlog::info("loading splash configuration from {}", config_path);
        raptor::ui::PageController page_controller {config};

        std::string boot_error;
        (void)raptor::ui::activate_boot_presentation(page_controller, config, boot_error);
        if (!boot_error.empty()) {
            spdlog::warn("boot presentation reported: {}", boot_error);
        }

        for (const auto& display_config : config.displays) {
            auto backend = raptor::ui::DisplayBackend::create(display_config);
            backend->initialize();

            auto snapshot = raptor::ui::initialize_snapshot(display_config, *backend, page_controller);
            ++snapshot.render_count;
            backend->render(snapshot);
        }

        return 0;
    } catch (const std::exception& ex) {
        raptor::ui::initialize_logging("raptor-native-splash-screen", "info");
        spdlog::critical("raptor-native-splash-screen failed: {}", ex.what());
        return 1;
    }
}
