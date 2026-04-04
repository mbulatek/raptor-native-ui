#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace raptor::ui {

struct MidiEventSummary {
    bool available {false};
    std::string module_id;
    int global_port {-1};
    std::string bytes_hex;
    std::uint64_t sequence {0};
    std::uint64_t timestamp_ns {0};
};

struct UpstreamStatus {
    bool reachable {false};
    std::string service;
    std::string summary;
    std::uint64_t timestamp_ns {0};
};

struct UiSnapshot {
    std::string display_id;
    std::string display_driver;
    std::string display_model;
    std::string layout;
    std::string page_id;
    std::string page_type;
    std::string page_title;
    std::string page_variant {"auto"};
    std::string page_image_path;
    std::uint16_t page_image_x {0};
    std::uint16_t page_image_y {0};
    std::uint16_t display_width {0};
    std::uint16_t display_height {0};
    std::uint64_t render_count {0};
    MidiEventSummary last_midi;
    UpstreamStatus sequencer;
};

class EventPublisher {
public:
    EventPublisher(std::string endpoint, std::string topic);
    ~EventPublisher();

    EventPublisher(const EventPublisher&) = delete;
    EventPublisher& operator=(const EventPublisher&) = delete;

    void publish_snapshot(const UiSnapshot& snapshot);

private:
    std::string endpoint_;
    std::string topic_;
    struct Impl;
    Impl* impl_ {nullptr};
};

class MidiEventSubscriber {
public:
    explicit MidiEventSubscriber(std::string endpoint);
    ~MidiEventSubscriber();

    MidiEventSubscriber(const MidiEventSubscriber&) = delete;
    MidiEventSubscriber& operator=(const MidiEventSubscriber&) = delete;

    bool poll_once(MidiEventSummary& summary);

private:
    std::string endpoint_;
    struct Impl;
    Impl* impl_ {nullptr};
};

class ControlClient {
public:
    explicit ControlClient(std::string endpoint);
    ~ControlClient();

    ControlClient(const ControlClient&) = delete;
    ControlClient& operator=(const ControlClient&) = delete;

    std::optional<UpstreamStatus> query_status(const std::string& request_id) const;

private:
    std::string endpoint_;
};

}  // namespace raptor::ui
