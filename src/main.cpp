#include "raptor_ui/application.hpp"
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
        bool once = false;
        std::string config_path;

        // Parse args:
        // - --once: render boot screen once on all displays and exit
        // - first non-flag arg: config path
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--once") {
                once = true;
                continue;
            }
            if (!arg.empty() && arg[0] == '-') {
                spdlog::warn("unknown flag ignored: {}", arg);
                continue;
            }
            if (config_path.empty()) {
                config_path = arg;
            } else {
                spdlog::warn("extra argument ignored: {}", arg);
            }
        }

        if (config_path.empty()) {
            config_path = "/etc/raptor-native-ui/raptor-native-ui.yaml";
        }

        auto config = raptor::ui::load_config(config_path);
        raptor::ui::initialize_logging("raptor-native-ui", config.logging.level);
        spdlog::info("loading config from {}", config_path);

        if (!once) {
            raptor::ui::Application app {std::move(config)};
            return app.run();
        }

        spdlog::info("--once: rendering boot screen on {} display(s) and exiting", config.displays.size());

        auto page_controller = std::make_shared<raptor::ui::PageController>(config);
        std::string boot_err;
        const bool boot_ok = raptor::ui::activate_boot_presentation(*page_controller, config, boot_err);
        if (!boot_ok) {
            spdlog::warn("--once: boot presentation not active: {}", boot_err);
        }

        for (const auto& display_config : config.displays) {
            spdlog::info("--once: init display id={} driver={} model={}", display_config.id, display_config.driver, display_config.model);
            auto backend = raptor::ui::DisplayBackend::create(display_config);
            backend->initialize();

            auto snapshot = raptor::ui::initialize_snapshot(display_config, *backend, *page_controller);
            if (!boot_ok) {
                snapshot.page_type = "boot";
                snapshot.page_title = "Boot";
                snapshot.page_variant = "auto";
                snapshot.page_image_path.clear();
                snapshot.page_image_x = 0;
                snapshot.page_image_y = 0;
            }

            backend->render(snapshot);
        }

        return 0;
    } catch (const std::exception& ex) {
        raptor::ui::initialize_logging("raptor-native-ui", "info");
        spdlog::critical("raptor-native-ui failed: {}", ex.what());
        return 1;
    }
}
