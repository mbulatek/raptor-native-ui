#include "raptor_ui/page_controller.hpp"

#include <algorithm>

namespace raptor::ui {
namespace {

bool page_supported_by_display(const PageConfig& page, const DisplayConfig& display) {
    return page.allowed_models.empty() ||
           std::find(page.allowed_models.begin(), page.allowed_models.end(), display.model) != page.allowed_models.end();
}

bool assignments_equal(const std::vector<DisplayAssignmentConfig>& lhs, const std::vector<DisplayAssignmentConfig>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (const auto& left : lhs) {
        const auto it = std::find_if(rhs.begin(), rhs.end(), [&](const auto& right) {
            return left.display_id == right.display_id && left.page_id == right.page_id;
        });
        if (it == rhs.end()) {
            return false;
        }
    }
    return true;
}

}  // namespace

PageController::PageController(const ServiceConfig& config)
    : pages_(config.pages), displays_(config.displays), assignments_(config.assignments), scenes_(config.scenes), active_scene_id_(config.initial_scene) {
    if (!active_scene_id_.empty()) {
        std::string error;
        if (!activate_scene(active_scene_id_, error)) {
            active_scene_id_.clear();
        }
    }
    if (active_scene_id_.empty()) {
        for (const auto& scene : scenes_) {
            if (assignments_equal(assignments_, scene.assignments)) {
                active_scene_id_ = scene.id;
                break;
            }
        }
    }
}

const std::vector<PageConfig>& PageController::pages() const {
    return pages_;
}

const std::vector<SceneConfig>& PageController::scenes() const {
    return scenes_;
}

std::vector<DisplayAssignmentConfig> PageController::assignments() const {
    return assignments_;
}

std::string PageController::active_scene_id() const {
    return active_scene_id_;
}

const DisplayConfig* PageController::display(const std::string& display_id) const {
    const auto it = std::find_if(displays_.begin(), displays_.end(), [&](const auto& display) {
        return display.id == display_id;
    });
    return it == displays_.end() ? nullptr : &*it;
}

const PageConfig* PageController::page_for_display(const std::string& display_id) const {
    const auto assignment_it = std::find_if(assignments_.begin(), assignments_.end(), [&](const auto& assignment) {
        return assignment.display_id == display_id;
    });
    if (assignment_it == assignments_.end()) {
        return nullptr;
    }

    const auto page_it = std::find_if(pages_.begin(), pages_.end(), [&](const auto& page) {
        return page.id == assignment_it->page_id;
    });
    return page_it == pages_.end() ? nullptr : &*page_it;
}

bool PageController::assign_page(const std::string& display_id, const std::string& page_id, std::string& error_message) {
    const auto* display_ptr = display(display_id);
    if (display_ptr == nullptr) {
        error_message = "unknown display";
        return false;
    }

    const auto page_it = std::find_if(pages_.begin(), pages_.end(), [&](const auto& page) {
        return page.id == page_id;
    });
    if (page_it == pages_.end()) {
        error_message = "unknown page";
        return false;
    }
    if (!page_supported_by_display(*page_it, *display_ptr)) {
        error_message = "page is not supported by display model";
        return false;
    }

    const auto assignment_it = std::find_if(assignments_.begin(), assignments_.end(), [&](const auto& assignment) {
        return assignment.display_id == display_id;
    });
    if (assignment_it == assignments_.end()) {
        assignments_.push_back(DisplayAssignmentConfig {.display_id = display_id, .page_id = page_id});
    } else {
        assignment_it->page_id = page_id;
    }
    active_scene_id_.clear();
    error_message.clear();
    return true;
}

bool PageController::swap_pages(const std::string& display_a, const std::string& display_b, std::string& error_message) {
    const auto* page_a = page_for_display(display_a);
    const auto* page_b = page_for_display(display_b);
    const auto* config_a = display(display_a);
    const auto* config_b = display(display_b);

    if (page_a == nullptr || page_b == nullptr || config_a == nullptr || config_b == nullptr) {
        error_message = "unknown display assignment";
        return false;
    }
    if (!page_supported_by_display(*page_a, *config_b) || !page_supported_by_display(*page_b, *config_a)) {
        error_message = "one of the pages is not supported by the target display";
        return false;
    }

    auto assignment_a = std::find_if(assignments_.begin(), assignments_.end(), [&](const auto& assignment) {
        return assignment.display_id == display_a;
    });
    auto assignment_b = std::find_if(assignments_.begin(), assignments_.end(), [&](const auto& assignment) {
        return assignment.display_id == display_b;
    });
    std::swap(assignment_a->page_id, assignment_b->page_id);
    active_scene_id_.clear();
    error_message.clear();
    return true;
}

bool PageController::activate_scene(const std::string& scene_id, std::string& error_message) {
    const auto scene_it = std::find_if(scenes_.begin(), scenes_.end(), [&](const auto& scene) {
        return scene.id == scene_id;
    });
    if (scene_it == scenes_.end()) {
        error_message = "unknown scene";
        return false;
    }
    assignments_ = scene_it->assignments;
    active_scene_id_ = scene_it->id;
    error_message.clear();
    return true;
}

}  // namespace raptor::ui
