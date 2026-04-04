#pragma once

#include "raptor_ui/config.hpp"

namespace raptor::ui {

class Application {
public:
    explicit Application(ServiceConfig config);
    int run();

private:
    ServiceConfig config_;
};

}  // namespace raptor::ui
