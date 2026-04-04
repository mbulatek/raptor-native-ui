#pragma once

#include <memory>
#include <string>
#include <vector>

#include "raptor_ui/config.hpp"
#include "raptor_ui/ipc.hpp"
#include "raptor_ui/page_controller.hpp"

namespace raptor::ui {

struct ServiceSnapshot {
    std::string service_name {"raptor-ui-service"};
    std::string ui_events_endpoint;
    std::string ui_control_endpoint;
    std::vector<UiSnapshot> displays;
};

class ControlServer {
public:
    ControlServer(std::string endpoint, const ServiceConfig& config, std::shared_ptr<PageController> page_controller);
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    void set_snapshot(ServiceSnapshot snapshot);
    void poll_once();

private:
    std::string endpoint_;
    const ServiceConfig* config_ {nullptr};
    std::shared_ptr<PageController> page_controller_;
    ServiceSnapshot snapshot_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace raptor::ui
