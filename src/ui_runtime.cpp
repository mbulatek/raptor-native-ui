#include "raptor_ui/ui_runtime.hpp"

#include <algorithm>

namespace raptor::ui {
namespace {

bool page_supported_by_display(const PageConfig& page, const DisplayConfig& display) {
    return page.allowed_models.empty() ||
           std::find(page.allowed_models.begin(), page.allowed_models.end(), display.model) != page.allowed_models.end();
}

}  // namespace

void apply_page_assignment(UiSnapshot& snapshot, const PageController& page_controller) {
    if (const auto* page = page_controller.page_for_display(snapshot.display_id)) {
        snapshot.page_id = page->id;
        snapshot.page_type = page->type;
        snapshot.page_title = page->title;
        snapshot.page_variant = page->default_variant;
        snapshot.page_image_path = page->image.path;
        snapshot.page_image_x = page->image.x;
        snapshot.page_image_y = page->image.y;
    } else {
        snapshot.page_id.clear();
        snapshot.page_type = "status";
        snapshot.page_title = "Status";
        snapshot.page_variant = "auto";
        snapshot.page_image_path.clear();
        snapshot.page_image_x = 0;
        snapshot.page_image_y = 0;
    }
}

UiSnapshot initialize_snapshot(const DisplayConfig& display_config,
                               const DisplayBackend& backend,
                               const PageController& page_controller) {
    UiSnapshot snapshot;
    snapshot.display_id = backend.id();
    snapshot.display_driver = display_config.driver;
    snapshot.display_model = backend.model();
    snapshot.layout = backend.layout();
    snapshot.display_width = backend.width();
    snapshot.display_height = backend.height();
    apply_page_assignment(snapshot, page_controller);
    return snapshot;
}

bool activate_boot_presentation(PageController& page_controller, const ServiceConfig& config, std::string& error_message) {
    if (std::any_of(page_controller.scenes().begin(), page_controller.scenes().end(), [](const SceneConfig& scene) {
            return scene.id == "boot";
        })) {
        return page_controller.activate_scene("boot", error_message);
    }

    bool assigned_any = false;
    for (const auto& display : config.displays) {
        const auto boot_page = std::find_if(page_controller.pages().begin(), page_controller.pages().end(), [&](const PageConfig& page) {
            return page.type == "boot" && page_supported_by_display(page, display);
        });
        if (boot_page == page_controller.pages().end()) {
            continue;
        }

        std::string assign_error;
        if (!page_controller.assign_page(display.id, boot_page->id, assign_error)) {
            error_message = assign_error;
            return false;
        }
        assigned_any = true;
    }

    if (!assigned_any) {
        error_message = "no boot scene or compatible boot pages found";
    } else {
        error_message.clear();
    }
    return assigned_any;
}

}  // namespace raptor::ui
