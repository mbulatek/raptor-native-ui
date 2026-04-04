#include "raptor_ui/application.hpp"

#include "raptor_ui/control_server.hpp"
#include "raptor_ui/display_backend.hpp"
#include "raptor_ui/ipc.hpp"
#include "raptor_ui/page_controller.hpp"
#include "raptor_ui/ui_runtime.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>

namespace raptor::ui {
namespace {

struct DisplayRuntime {
    const DisplayConfig* config {nullptr};
    std::unique_ptr<DisplayBackend> backend;
    UiSnapshot snapshot;
    std::chrono::steady_clock::time_point next_render;
};

}  // namespace

Application::Application(ServiceConfig config) : config_(std::move(config)) {}

int Application::run() {
    try {
    spdlog::debug("ui start displays={} ui_events={} ui_control={} midi_events={} seq_control={}", config_.displays.size(), config_.ipc.ui_events_endpoint, config_.ipc.ui_control_endpoint, config_.ipc.midi_events_endpoint, config_.ipc.sequencer_control_endpoint);
    auto page_controller = std::make_shared<PageController>(config_);

    std::vector<DisplayRuntime> displays;
    displays.reserve(config_.displays.size());

    for (const auto& display_config : config_.displays) {
        spdlog::debug("ui display init id={} driver={} model={} refresh_ms={}", display_config.id, display_config.driver, display_config.model, display_config.refresh_period_ms);
        auto backend = DisplayBackend::create(display_config);
        backend->initialize();

        auto snapshot = initialize_snapshot(display_config, *backend, *page_controller);

        displays.push_back(DisplayRuntime {
            .config = &display_config,
            .backend = std::move(backend),
            .snapshot = std::move(snapshot),
            .next_render = std::chrono::steady_clock::now(),
        });
    }

    EventPublisher publisher {config_.ipc.ui_events_endpoint, "ui.snapshot"};
    MidiEventSubscriber midi_subscriber {config_.ipc.midi_events_endpoint};
    ControlClient sequencer_client {config_.ipc.sequencer_control_endpoint};
    ControlServer control_server {config_.ipc.ui_control_endpoint, config_, page_controller};

    ServiceSnapshot service_snapshot;
    service_snapshot.ui_events_endpoint = config_.ipc.ui_events_endpoint;
    service_snapshot.ui_control_endpoint = config_.ipc.ui_control_endpoint;

    auto next_sequencer_poll = std::chrono::steady_clock::now();

    while (true) {
        MidiEventSummary midi;
        if (midi_subscriber.poll_once(midi)) {
            for (auto& display : displays) {
                display.snapshot.last_midi = midi;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_sequencer_poll) {
            const auto status = sequencer_client.query_status("ui-service-sequencer-status");
            for (auto& display : displays) {
                if (status.has_value()) {
                    display.snapshot.sequencer = *status;
                } else {
                    display.snapshot.sequencer.reachable = false;
                    display.snapshot.sequencer.service = "raptor-sequencer";
                    display.snapshot.sequencer.summary = "unreachable";
                    display.snapshot.sequencer.timestamp_ns = 0;
                }
            }
            next_sequencer_poll = now + std::chrono::seconds(1);
        }

        for (auto& display : displays) {
            apply_page_assignment(display.snapshot, *page_controller);
            if (now >= display.next_render) {
                ++display.snapshot.render_count;
                spdlog::debug("ui render display={} page={} type={} variant={} count={}", display.snapshot.display_id, display.snapshot.page_id, display.snapshot.page_type, display.snapshot.page_variant, display.snapshot.render_count);
                display.backend->render(display.snapshot);
                publisher.publish_snapshot(display.snapshot);
                display.next_render = now + std::chrono::milliseconds(display.config->refresh_period_ms);
            }
        }

        service_snapshot.displays.clear();
        for (const auto& display : displays) {
            service_snapshot.displays.push_back(display.snapshot);
        }
        control_server.set_snapshot(service_snapshot);
        control_server.poll_once();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    } catch (const std::exception& ex) {
        spdlog::error("ui fatal error: {}", ex.what());
        return 1;
    }
}

}  // namespace raptor::ui
