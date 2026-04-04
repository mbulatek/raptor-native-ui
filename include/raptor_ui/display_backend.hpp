#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "raptor_ui/config.hpp"
#include "raptor_ui/ipc.hpp"

namespace raptor::ui {

class DisplayBackend {
public:
    virtual ~DisplayBackend() = default;

    virtual void initialize() = 0;
    virtual void render(const UiSnapshot& snapshot) = 0;
    virtual void clear() = 0;
    virtual std::uint16_t width() const = 0;
    virtual std::uint16_t height() const = 0;
    virtual const std::string& id() const = 0;
    virtual const std::string& model() const = 0;
    virtual const std::string& layout() const = 0;

    static std::unique_ptr<DisplayBackend> create(const DisplayConfig& config);
};

}  // namespace raptor::ui
