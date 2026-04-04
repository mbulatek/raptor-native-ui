#pragma once

#include <memory>
#include <string>
#include <vector>

#include "raptor_ui/config.hpp"

namespace raptor::ui {

class PageController {
public:
    explicit PageController(const ServiceConfig& config);

    const std::vector<PageConfig>& pages() const;
    const std::vector<SceneConfig>& scenes() const;
    std::vector<DisplayAssignmentConfig> assignments() const;
    std::string active_scene_id() const;

    const PageConfig* page_for_display(const std::string& display_id) const;
    const DisplayConfig* display(const std::string& display_id) const;

    bool assign_page(const std::string& display_id, const std::string& page_id, std::string& error_message);
    bool swap_pages(const std::string& display_a, const std::string& display_b, std::string& error_message);
    bool activate_scene(const std::string& scene_id, std::string& error_message);

private:
    std::vector<PageConfig> pages_;
    std::vector<DisplayConfig> displays_;
    std::vector<DisplayAssignmentConfig> assignments_;
    std::vector<SceneConfig> scenes_;
    std::string active_scene_id_;
};

}  // namespace raptor::ui
