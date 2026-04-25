#include "raptor_ui/ipc.hpp"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <zmq.h>

namespace raptor::ui {
namespace {

using json = nlohmann::json;

constexpr char kSchemaVersion[] = "1.0";
constexpr char kServiceName[] = "raptor-ui-service";
constexpr char kMidiTopic[] = "midi.packet";

std::filesystem::path endpoint_directory(const std::string& endpoint) {
    constexpr std::string_view prefix {"ipc://"};
    if (!endpoint.starts_with(prefix)) {
        return {};
    }
    return std::filesystem::path {endpoint.substr(prefix.size())}.parent_path();
}

std::uint64_t monotonic_time_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

json midi_json(const MidiEventSummary& midi) {
    return {
        {"available", midi.available},
        {"module_id", midi.module_id},
        {"global_port", midi.global_port},
        {"bytes_hex", midi.bytes_hex},
        {"sequence", midi.sequence},
        {"timestamp_ns", midi.timestamp_ns},
    };
}

json sequencer_json(const UpstreamStatus& status) {
    json j = {
        {"reachable", status.reachable},
        {"service", status.service},
        {"summary", status.summary},
        {"timestamp_ns", status.timestamp_ns},
    };
    if (status.tick.has_value()) {
        j["tick"] = *status.tick;
    }
    if (status.bpm.has_value()) {
        j["bpm"] = *status.bpm;
    }
    if (!status.transport.empty()) {
        j["transport"] = status.transport;
    }
    if (!status.active_pattern.empty()) {
        j["active_pattern"] = status.active_pattern;
    }
    if (status.active_step.has_value()) {
        j["active_step"] = *status.active_step;
    }
    if (status.bar.has_value()) {
        j["bar"] = *status.bar;
    }
    if (status.bars_total.has_value()) {
        j["bars_total"] = *status.bars_total;
    }
    if (status.beat.has_value()) {
        j["beat"] = *status.beat;
    }
    if (status.beats_per_bar.has_value()) {
        j["beats_per_bar"] = *status.beats_per_bar;
    }
    if (status.beat_unit.has_value()) {
        j["beat_unit"] = *status.beat_unit;
    }
    if (status.active_clip_index.has_value()) {
        j["active_clip_index"] = *status.active_clip_index;
    }
    if (status.midi_in_port.has_value()) {
        j["midi_in_port"] = *status.midi_in_port;
    }
    if (status.midi_in_channel.has_value()) {
        j["midi_in_channel"] = *status.midi_in_channel;
    }
    if (status.midi_out_port.has_value()) {
        j["midi_out_port"] = *status.midi_out_port;
    }
    if (status.midi_out_channel.has_value()) {
        j["midi_out_channel"] = *status.midi_out_channel;
    }
    if (!status.recording_quantize.empty()) {
        j["recording_quantize"] = status.recording_quantize;
    }
    if (status.song.available) {
        json tracks = json::array();
        for (const auto& track : status.song.tracks) {
            tracks.push_back({
                {"id", track.id},
                {"name", track.name},
                {"muted", track.muted},
                {"midi_in", track.midi_in},
                {"midi_out", track.midi_out},
                {"midi_channel_in", track.midi_channel_in},
                {"midi_channel_out", track.midi_channel_out},
            });
        }
        j["song"] = {
            {"available", true},
            {"id", status.song.id},
            {"title", status.song.title},
            {"tracks", std::move(tracks)},
        };
    }
    return j;
}

json snapshot_json(const UiSnapshot& snapshot) {
    return {
        {"schema_version", kSchemaVersion},
        {"service", kServiceName},
        {"type", "ui.snapshot"},
        {"timestamp_ns", monotonic_time_ns()},
        {"display",
         {
             {"id", snapshot.display_id},
             {"driver", snapshot.display_driver},
             {"model", snapshot.display_model},
             {"layout", snapshot.layout},
             {"width", snapshot.display_width},
             {"height", snapshot.display_height},
         }},
        {"page",
         {
             {"id", snapshot.page_id},
             {"type", snapshot.page_type},
             {"title", snapshot.page_title},
             {"variant", snapshot.page_variant},
             {"image_path", snapshot.page_image_path},
             {"image_x", snapshot.page_image_x},
             {"image_y", snapshot.page_image_y},
         }},
        {"render_count", snapshot.render_count},
        {"last_midi", midi_json(snapshot.last_midi)},
        {"sequencer", sequencer_json(snapshot.sequencer)},
    };
}

std::optional<json> request_control_json(const std::string& endpoint, const json& request, const int timeout_ms) {
    void* context = zmq_ctx_new();
    if (context == nullptr) {
        return std::nullopt;
    }

    void* socket = zmq_socket(context, ZMQ_REQ);
    if (socket == nullptr) {
        zmq_ctx_term(context);
        return std::nullopt;
    }

    constexpr int linger_ms = 0;
    (void)zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    (void)zmq_setsockopt(socket, ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    (void)zmq_setsockopt(socket, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));

    if (zmq_connect(socket, endpoint.c_str()) != 0) {
        zmq_close(socket);
        zmq_ctx_term(context);
        return std::nullopt;
    }

    const auto payload = request.dump();
    if (zmq_send(socket, payload.data(), payload.size(), 0) < 0) {
        zmq_close(socket);
        zmq_ctx_term(context);
        return std::nullopt;
    }

    std::vector<char> buffer(64 * 1024, 0);
    const auto size = zmq_recv(socket, buffer.data(), buffer.size(), 0);
    zmq_close(socket);
    zmq_ctx_term(context);
    if (size <= 0) {
        return std::nullopt;
    }

    try {
        return json::parse(std::string(buffer.data(), buffer.data() + size));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

SequencerSongSummary parse_song_summary_from_project(const json& project_json) {
    SequencerSongSummary song;
    if (!project_json.is_object()) {
        return song;
    }

    if (!project_json.contains("song") || !project_json["song"].is_object()) {
        return song;
    }
    const auto& s = project_json["song"];
    song.id = s.value("id", std::string{});
    song.title = s.value("title", std::string{});

    if (s.contains("tracks") && s["tracks"].is_array()) {
        for (const auto& t : s["tracks"]) {
            if (!t.is_object()) {
                continue;
            }
            SequencerTrackSummary track;
            track.id = t.value("id", std::string{});
            track.name = t.value("name", std::string{});
            track.muted = t.value("muted", false);
            track.midi_in = t.value("midi_in", std::string{});
            track.midi_out = t.value("midi_out", std::string{});
            if (t.contains("midi_channel_in")) {
                track.midi_channel_in = t.value("midi_channel_in", -1);
            } else if (t.contains("midi_channel")) {
                track.midi_channel_in = t.value("midi_channel", -1);
            }
            if (t.contains("midi_channel_out")) {
                track.midi_channel_out = t.value("midi_channel_out", -1);
            } else if (t.contains("midi_channel")) {
                track.midi_channel_out = t.value("midi_channel", -1);
            }
            song.tracks.push_back(std::move(track));
        }
    }

    song.available = true;
    return song;
}

}  // namespace

struct EventPublisher::Impl {
    void* context {nullptr};
    void* socket {nullptr};
};

EventPublisher::EventPublisher(std::string endpoint, std::string topic)
    : endpoint_(std::move(endpoint)), topic_(std::move(topic)), impl_(new Impl()) {
    spdlog::debug("ui pub init endpoint={} topic_prefix={}", endpoint_, topic_);
    const auto directory = endpoint_directory(endpoint_);
    if (!directory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
    }
    impl_->context = zmq_ctx_new();
    if (impl_->context == nullptr) {
        spdlog::error("zmq_ctx_new failed for ui publisher {}: {}", endpoint_, zmq_strerror(zmq_errno()));
        return;
    }
    impl_->socket = zmq_socket(impl_->context, ZMQ_PUB);
    if (impl_->socket == nullptr) {
        spdlog::error("zmq_socket(ZMQ_PUB) failed for ui publisher {}: {}", endpoint_, zmq_strerror(zmq_errno()));
        zmq_ctx_term(impl_->context);
        impl_->context = nullptr;
        return;
    }
    constexpr int linger_ms = 0;
    (void)zmq_setsockopt(impl_->socket, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
    // Bind failures are non-fatal for early boot; caller can still run in degraded mode.
    if (zmq_bind(impl_->socket, endpoint_.c_str()) != 0) {
        spdlog::error("zmq_bind failed for ui publisher {}: {}", endpoint_, zmq_strerror(zmq_errno()));
        zmq_close(impl_->socket);
        zmq_ctx_term(impl_->context);
        impl_->socket = nullptr;
        impl_->context = nullptr;
    }
}

EventPublisher::~EventPublisher() {
// If the socket is alive here, bind succeeded.

    if (impl_ != nullptr) {
        if (impl_->socket != nullptr) {
            zmq_close(impl_->socket);
        }
        if (impl_->context != nullptr) {
            zmq_ctx_term(impl_->context);
        }
    }
    delete impl_;
}

void EventPublisher::publish_snapshot(const UiSnapshot& snapshot) {
    if (!(impl_ != nullptr && impl_->socket != nullptr)) {
        return;
    }

    const auto payload = snapshot_json(snapshot).dump();
    const auto topic = topic_ + "." + snapshot.display_id;
    const auto rc1 = zmq_send(impl_->socket, topic.data(), topic.size(), ZMQ_SNDMORE);
    const auto rc2 = zmq_send(impl_->socket, payload.data(), payload.size(), 0);
    if (rc1 >= 0 && rc2 >= 0) {
        spdlog::trace("ui snapshot pub endpoint={} topic={} bytes={}", endpoint_, topic, payload.size());
    } else {
        static std::uint64_t publish_failures = 0;
        ++publish_failures;
        if (publish_failures == 1 || (publish_failures % 100) == 0) {
            spdlog::error(
                "ui snapshot publish failed endpoint={} err={} failures={}",
                endpoint_,
                zmq_strerror(zmq_errno()),
                publish_failures);
        }
    }
    (void)rc1;
    (void)rc2;
}

struct MidiEventSubscriber::Impl {
    void* context {nullptr};
    void* socket {nullptr};
};

MidiEventSubscriber::MidiEventSubscriber(std::string endpoint)
    : endpoint_(std::move(endpoint)), impl_(new Impl()) {
    spdlog::debug("midi sub connect endpoint={}", endpoint_);
    impl_->context = zmq_ctx_new();
    if (impl_->context == nullptr) {
        spdlog::error("zmq_ctx_new failed for midi subscriber {}: {}", endpoint_, zmq_strerror(zmq_errno()));
        return;
    }
    impl_->socket = zmq_socket(impl_->context, ZMQ_SUB);
    if (impl_->socket == nullptr) {
        spdlog::error("zmq_socket(ZMQ_SUB) failed for midi subscriber {}: {}", endpoint_, zmq_strerror(zmq_errno()));
        zmq_ctx_term(impl_->context);
        impl_->context = nullptr;
        return;
    }
    constexpr int recv_timeout_ms = 0;
    (void)zmq_setsockopt(impl_->socket, ZMQ_RCVTIMEO, &recv_timeout_ms, sizeof(recv_timeout_ms));
    (void)zmq_setsockopt(impl_->socket, ZMQ_SUBSCRIBE, kMidiTopic, sizeof(kMidiTopic) - 1);
    if (zmq_connect(impl_->socket, endpoint_.c_str()) != 0) {
        spdlog::error("zmq_connect failed for midi subscriber {}: {}", endpoint_, zmq_strerror(zmq_errno()));
        zmq_close(impl_->socket);
        zmq_ctx_term(impl_->context);
        impl_->socket = nullptr;
        impl_->context = nullptr;
    }
}

MidiEventSubscriber::~MidiEventSubscriber() {
    if (impl_ != nullptr) {
        if (impl_->socket != nullptr) {
            zmq_close(impl_->socket);
        }
        if (impl_->context != nullptr) {
            zmq_ctx_term(impl_->context);
        }
    }
    delete impl_;
}

bool MidiEventSubscriber::poll_once(MidiEventSummary& summary) {
    if (!(impl_ != nullptr && impl_->socket != nullptr)) {
        return false;
    }

    zmq_pollitem_t items[] = {{impl_->socket, 0, ZMQ_POLLIN, 0}};
    if (zmq_poll(items, 1, 0) <= 0 || (items[0].revents & ZMQ_POLLIN) == 0) {
        return false;
    }

    zmq_msg_t topic;
    zmq_msg_t payload;
    zmq_msg_init(&topic);
    zmq_msg_init(&payload);
    if (zmq_msg_recv(&topic, impl_->socket, 0) < 0 || zmq_msg_recv(&payload, impl_->socket, 0) < 0) {
        static std::uint64_t recv_failures = 0;
        ++recv_failures;
        if (recv_failures == 1 || (recv_failures % 100) == 0) {
            spdlog::error(
                "midi sub recv failed endpoint={} err={} failures={}",
                endpoint_,
                zmq_strerror(zmq_errno()),
                recv_failures);
        }
        zmq_msg_close(&topic);
        zmq_msg_close(&payload);
        return false;
    }

    const auto* raw = static_cast<const char*>(zmq_msg_data(&payload));
    const std::string text {raw, raw + zmq_msg_size(&payload)};
    zmq_msg_close(&topic);
    zmq_msg_close(&payload);

    try {
        const auto root = nlohmann::json::parse(text);
        summary.available = true;
        summary.module_id = root["source"].value("module_id", "");
        summary.global_port = root["source"].value("global_port", -1);
        summary.bytes_hex = root["midi"].value("bytes_hex", "");
        summary.sequence = root.value("sequence", static_cast<std::uint64_t>(0));
        summary.timestamp_ns = root.value("timestamp_ns", static_cast<std::uint64_t>(0));
        spdlog::debug("midi event seq={} module={} port={} bytes=""{}""", summary.sequence, summary.module_id, summary.global_port, summary.bytes_hex);
        return true;
    } catch (const std::exception& ex) {
        static std::uint64_t parse_failures = 0;
        ++parse_failures;
        if (parse_failures == 1 || (parse_failures % 100) == 0) {
            spdlog::error(
                "midi sub json parse failed endpoint={} err={} failures={}",
                endpoint_,
                ex.what(),
                parse_failures);
        }
        return false;
    }
}

ControlClient::ControlClient(std::string endpoint) : endpoint_(std::move(endpoint)) {}
ControlClient::~ControlClient() = default;

std::optional<UpstreamStatus> ControlClient::query_status(const std::string& request_id) const {
    spdlog::trace("sequencer status query endpoint={} request_id={}", endpoint_, request_id);
    constexpr int timeout_ms = 100;
    const auto reply_opt = request_control_json(endpoint_, json{{"command", "status"}, {"request_id", request_id}}, timeout_ms);
    if (!reply_opt.has_value()) {
        static std::uint64_t failures = 0;
        ++failures;
        if (failures == 1 || (failures % 30) == 0) {
            spdlog::warn("sequencer status query failed endpoint={} failures={}", endpoint_, failures);
        }
        return std::nullopt;
    }

    try {
        const auto& reply = *reply_opt;
        UpstreamStatus status;
        status.reachable = reply.value("ok", false);
        status.service = reply.value("service", "unknown");
        status.timestamp_ns = reply.value("timestamp_ns", static_cast<std::uint64_t>(0));
        if (reply.contains("snapshot") && reply["snapshot"].is_object()) {
            const auto& snap = reply["snapshot"];
            if (snap.contains("tick")) {
                status.tick = snap.value("tick", static_cast<std::uint64_t>(0));
            }
            if (snap.contains("bpm")) {
                status.bpm = snap.value("bpm", 0.0);
            }
            status.transport = snap.value("transport", "");
            status.active_pattern = snap.value("active_pattern", "");
            if (snap.contains("active_step")) {
                status.active_step = snap.value("active_step", static_cast<std::uint32_t>(0));
            }

            if (snap.contains("bar")) {
                status.bar = snap.value("bar", static_cast<std::uint32_t>(0));
            }
            if (snap.contains("bars_total")) {
                status.bars_total = snap.value("bars_total", static_cast<std::uint32_t>(0));
            }
            if (snap.contains("beat")) {
                status.beat = snap.value("beat", static_cast<std::uint32_t>(0));
            }
            if (snap.contains("beats_per_bar")) {
                status.beats_per_bar = snap.value("beats_per_bar", static_cast<std::uint32_t>(0));
            }
            if (snap.contains("beat_unit")) {
                status.beat_unit = snap.value("beat_unit", static_cast<std::uint32_t>(0));
            }
            if (snap.contains("active_clip_index")) {
                status.active_clip_index = snap.value("active_clip_index", static_cast<std::uint32_t>(0));
            }
            if (snap.contains("midi_in_port")) {
                status.midi_in_port = snap.value("midi_in_port", -1);
            }
            if (snap.contains("midi_in_channel")) {
                status.midi_in_channel = snap.value("midi_in_channel", -1);
            }
            if (snap.contains("midi_out_port")) {
                status.midi_out_port = snap.value("midi_out_port", -1);
            }
            if (snap.contains("midi_out_channel")) {
                status.midi_out_channel = snap.value("midi_out_channel", -1);
            }
            if (snap.contains("recording_quantize")) {
                status.recording_quantize = snap.value("recording_quantize", std::string{});
            }
        }

        if (status.reachable && reply.contains("data")) {
            status.summary = "status ok";
        } else if (reply.contains("error")) {
            status.summary = reply["error"].value("message", "upstream error");
        }
        return status;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<SequencerSongSummary> ControlClient::query_project(const std::string& request_id) const {
    spdlog::debug("sequencer project query endpoint={} request_id={}", endpoint_, request_id);
    constexpr int timeout_ms = 150;
    const auto reply_opt = request_control_json(endpoint_, json{{"command", "get-project"}, {"request_id", request_id}}, timeout_ms);
    if (!reply_opt.has_value()) {
        return std::nullopt;
    }

    try {
        const auto& reply = *reply_opt;
        if (!reply.value("ok", false)) {
            return std::nullopt;
        }
        if (!reply.contains("data") || !reply["data"].is_object()) {
            return std::nullopt;
        }
        const auto& data = reply["data"];
        if (!data.contains("project")) {
            return std::nullopt;
        }
        return parse_song_summary_from_project(data["project"]);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace raptor::ui
