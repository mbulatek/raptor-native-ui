#include "raptor_ui/display_backend.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>

extern "C" {
#include "GUI_Paint.h"
#include "fonts.h"
#include "raptor_ui/waveshare_shim.h"
}

namespace raptor::ui {
namespace {

#pragma pack(push, 1)
struct BmpFileHeader {
    std::uint16_t type;
    std::uint32_t size;
    std::uint16_t reserved1;
    std::uint16_t reserved2;
    std::uint32_t offset;
};

struct BmpInfoHeader {
    std::uint32_t size;
    std::int32_t width;
    std::int32_t height;
    std::uint16_t planes;
    std::uint16_t bit_count;
    std::uint32_t compression;
    std::uint32_t image_size;
    std::int32_t x_ppm;
    std::int32_t y_ppm;
    std::uint32_t clr_used;
    std::uint32_t clr_important;
};

struct BmpPaletteEntry {
    std::uint8_t blue;
    std::uint8_t green;
    std::uint8_t red;
    std::uint8_t reserved;
};
#pragma pack(pop)

std::size_t framebuffer_bytes(const waveshare_display_descriptor_t& descriptor) {
    if (descriptor.scale == 16) {
        return static_cast<std::size_t>((descriptor.width / 2) * descriptor.height);
    }
    return static_cast<std::size_t>(((descriptor.width + 7) / 8) * descriptor.height);
}

std::string choose_layout(const DisplayConfig& config, const waveshare_display_descriptor_t& descriptor) {
    if (config.layout != "auto") {
        return config.layout;
    }
    if (descriptor.height <= 32) {
        return "compact";
    }
    if (descriptor.width <= 64 && descriptor.height >= 96) {
        return "portrait_status";
    }
    if (descriptor.width == descriptor.height) {
        return "square_status";
    }
    return "wide_status";
}

std::string resolve_page_layout(const UiSnapshot& snapshot, const std::string& display_layout) {
    const auto& variant = snapshot.page_variant;
    if (variant.empty() || variant == "auto") {
        return display_layout;
    }
    if (variant == "compact" || variant == "portrait_status" || variant == "square_status" || variant == "wide_status") {
        return variant;
    }
    if (variant == "portrait") {
        return "portrait_status";
    }
    if (variant == "square") {
        return "square_status";
    }
    if (variant == "wide") {
        return "wide_status";
    }
    return display_layout;
}

bool draw_monochrome_bmp(const UiSnapshot& snapshot) {
    if (snapshot.page_image_path.empty()) {
        return false;
    }

    std::ifstream file(snapshot.page_image_path, std::ios::binary);
    if (!file) {
        spdlog::debug("bmp open failed path={}", snapshot.page_image_path);
        return false;
    }

    BmpFileHeader file_header {};
    BmpInfoHeader info_header {};
    file.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    file.read(reinterpret_cast<char*>(&info_header), sizeof(info_header));
    if (!file || file_header.type != 0x4D42 || info_header.size < sizeof(BmpInfoHeader)) {
        return false;
    }
    if (info_header.planes != 1 || info_header.bit_count != 1 || info_header.compression != 0) {
        return false;
    }

    BmpPaletteEntry palette[2] {};
    file.read(reinterpret_cast<char*>(palette), sizeof(palette));
    if (!file) {
        return false;
    }

    const bool top_down = info_header.height < 0;
    const int width = info_header.width;
    const int height = top_down ? -info_header.height : info_header.height;
    if (width <= 0 || height <= 0) {
        return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>((width + 7) / 8);
    const std::size_t padded_row_bytes = ((row_bytes + 3u) / 4u) * 4u;
    std::vector<std::uint8_t> row(padded_row_bytes, 0);

    const bool palette_zero_is_white = palette[0].red == 0xFF && palette[0].green == 0xFF && palette[0].blue == 0xFF;
    const auto set_pixel_from_bit = [&](int x, int y, bool bit_set) {
        const auto color = bit_set ? (palette_zero_is_white ? BLACK : WHITE)
                                   : (palette_zero_is_white ? WHITE : BLACK);
        Paint_SetPixel(snapshot.page_image_x + static_cast<std::uint16_t>(x), snapshot.page_image_y + static_cast<std::uint16_t>(y), color);
    };

    file.seekg(static_cast<std::streamoff>(file_header.offset), std::ios::beg);
    if (!file) {
        return false;
    }

    for (int source_row = 0; source_row < height; ++source_row) {
        file.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(padded_row_bytes));
        if (!file) {
            return false;
        }
        const int y = top_down ? source_row : (height - 1 - source_row);
        for (int x = 0; x < width; ++x) {
            const auto byte = row[static_cast<std::size_t>(x / 8)];
            const bool bit_set = (byte & (0x80u >> (x % 8))) != 0;
            set_pixel_from_bit(x, y, bit_set);
        }
    }

    return true;
}

void draw_status_page(const UiSnapshot& snapshot, const std::string& layout) {
    if (layout == "compact") {
        char line1[48];
        char line2[48];
        std::snprintf(line1, sizeof(line1), "%s %s", snapshot.page_title.c_str(), snapshot.sequencer.reachable ? "SEQ" : "OFF");
        if (snapshot.last_midi.available) {
            std::snprintf(line2, sizeof(line2), "P%d %s", snapshot.last_midi.global_port, snapshot.last_midi.bytes_hex.c_str());
        } else {
            std::snprintf(line2, sizeof(line2), "waiting");
        }
        Paint_DrawString_EN(0, 0, line1, &Font8, WHITE, BLACK);
        Paint_DrawString_EN(0, 12, line2, &Font8, WHITE, BLACK);
        return;
    }

    if (layout == "portrait_status") {
        char line1[48];
        char line2[48];
        char line3[48];
        std::snprintf(line1, sizeof(line1), "%s", snapshot.page_title.c_str());
        std::snprintf(line2, sizeof(line2), "SEQ %s", snapshot.sequencer.reachable ? "UP" : "DOWN");
        if (snapshot.last_midi.available) {
            std::snprintf(line3, sizeof(line3), "P%d %s", snapshot.last_midi.global_port, snapshot.last_midi.bytes_hex.c_str());
        } else {
            std::snprintf(line3, sizeof(line3), "MIDI idle");
        }
        Paint_DrawString_EN(0, 0, line1, &Font12, WHITE, BLACK);
        Paint_DrawString_EN(0, 20, line2, &Font12, WHITE, BLACK);
        Paint_DrawString_EN(0, 40, line3, &Font8, WHITE, BLACK);
        return;
    }

    if (layout == "wide_status") {
        char line1[64];
        char line2[64];
        char line3[64];
        std::snprintf(line1, sizeof(line1), "%s %s", snapshot.display_id.c_str(), snapshot.display_model.c_str());
        std::snprintf(line2, sizeof(line2), "SEQ: %s", snapshot.sequencer.reachable ? snapshot.sequencer.service.c_str() : "offline");
        if (snapshot.last_midi.available) {
            std::snprintf(line3, sizeof(line3), "MIDI P%d %s", snapshot.last_midi.global_port, snapshot.last_midi.bytes_hex.c_str());
        } else {
            std::snprintf(line3, sizeof(line3), "MIDI waiting for data");
        }
        waveshare_display_draw_status(snapshot.page_title.c_str(), line1, line2, line3);
        return;
    }

    char line1[64];
    char line2[64];
    char line3[64];
    std::snprintf(line1, sizeof(line1), "SEQ %s", snapshot.sequencer.reachable ? "UP" : "DOWN");
    if (snapshot.last_midi.available) {
        std::snprintf(line2, sizeof(line2), "P%d %s", snapshot.last_midi.global_port, snapshot.last_midi.bytes_hex.c_str());
    } else {
        std::snprintf(line2, sizeof(line2), "MIDI idle");
    }
    std::snprintf(line3, sizeof(line3), "%s #%llu", snapshot.display_id.c_str(), static_cast<unsigned long long>(snapshot.render_count));
    waveshare_display_draw_status(snapshot.page_title.c_str(), line1, line2, line3);
}

std::optional<std::uint8_t> parse_first_hex_byte(const std::string& hex) {
    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        return -1;
    };

    int hi = -1;
    for (char c : hex) {
        const int v = hex_value(c);
        if (v < 0) {
            continue;
        }
        if (hi < 0) {
            hi = v;
            continue;
        }
        const int lo = v;
        return static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return std::nullopt;
}

int midi_channel_from_status(std::uint8_t status) {
    if (status >= 0x80 && status <= 0xEF) {
        return static_cast<int>((status & 0x0F) + 1);
    }
    return -1;
}

int clip_index_from_active_pattern(const std::string& pattern) {
    // Expect patterns like A01/B12; fall back to 1.
    int value = 0;
    bool any = false;
    for (char c : pattern) {
        if (c >= '0' && c <= '9') {
            any = true;
            value = (value * 10) + (c - '0');
        }
    }
    if (!any || value <= 0) {
        return 1;
    }
    return value;
}

void draw_recording_page(const UiSnapshot& snapshot, const std::string& layout) {
    if (layout == "compact") {
        Paint_DrawString_EN(0, 0, "REC", &Font8, WHITE, BLACK);
        Paint_DrawString_EN(0, 12, snapshot.last_midi.available ? snapshot.last_midi.bytes_hex.c_str() : "NO MIDI", &Font8, WHITE, BLACK);
        return;
    }

    const int clip_index = snapshot.sequencer.active_clip_index.has_value() && *snapshot.sequencer.active_clip_index > 0
                               ? static_cast<int>(*snapshot.sequencer.active_clip_index)
                               : clip_index_from_active_pattern(snapshot.sequencer.active_pattern);

    const auto status_byte = snapshot.last_midi.available ? parse_first_hex_byte(snapshot.last_midi.bytes_hex) : std::nullopt;
    const int midi_ch = status_byte.has_value() ? midi_channel_from_status(*status_byte) : -1;

    // Prefer sequencer-derived musical position when available; otherwise fall back to prototype mapping.
    const std::uint64_t tick = snapshot.sequencer.tick.value_or(0);
    const std::uint32_t step = snapshot.sequencer.active_step.value_or(static_cast<std::uint32_t>(tick % 16ULL));

    const std::uint32_t bar = snapshot.sequencer.bar.value_or(static_cast<std::uint32_t>((tick / 16ULL) + 1ULL));
    const std::uint32_t bars_total = snapshot.sequencer.bars_total.value_or(8U);
    const std::uint32_t beat = snapshot.sequencer.beat.value_or(static_cast<std::uint32_t>((step / 4U) + 1U));
    const std::uint32_t beats_per_bar = snapshot.sequencer.beats_per_bar.value_or(4U);

    char clip_line[32];
    char in_line[48];
    char out_line[48];
    char bar_line[32];
    char beat_line[32];

    std::snprintf(clip_line, sizeof(clip_line), "Clip %d", clip_index);

    const int in_port = snapshot.sequencer.midi_in_port.value_or(-1);
    const int in_ch = snapshot.sequencer.midi_in_channel.value_or(-1);
    if (in_port >= 0) {
        if (in_ch > 0) {
            std::snprintf(in_line, sizeof(in_line), "IN  P%d  CH%d", in_port, in_ch);
        } else {
            std::snprintf(in_line, sizeof(in_line), "IN  P%d", in_port);
        }
    } else if (snapshot.last_midi.available) {
        if (midi_ch > 0) {
            std::snprintf(in_line, sizeof(in_line), "IN  P%d  CH%d", snapshot.last_midi.global_port, midi_ch);
        } else {
            std::snprintf(in_line, sizeof(in_line), "IN  P%d", snapshot.last_midi.global_port);
        }
    } else {
        std::snprintf(in_line, sizeof(in_line), "IN  ---");
    }

    const int out_port = snapshot.sequencer.midi_out_port.value_or(-1);
    const int out_ch = snapshot.sequencer.midi_out_channel.value_or(-1);
    if (out_port >= 0) {
        if (out_ch > 0) {
            std::snprintf(out_line, sizeof(out_line), "OUT P%d  CH%d", out_port, out_ch);
        } else {
            std::snprintf(out_line, sizeof(out_line), "OUT P%d", out_port);
        }
    } else {
        std::snprintf(out_line, sizeof(out_line), "OUT ---");
    }

    std::snprintf(bar_line, sizeof(bar_line), "Bar: %u/%u", bar, bars_total);
    std::snprintf(beat_line, sizeof(beat_line), "Beat: %u/%u", beat, beats_per_bar);

    Paint_DrawString_EN(0, 0, snapshot.page_title.c_str(), &Font12, WHITE, BLACK);
    Paint_DrawString_EN(0, 18, clip_line, &Font16, WHITE, BLACK);
    Paint_DrawString_EN(0, 44, in_line, &Font12, WHITE, BLACK);
    Paint_DrawString_EN(0, 60, out_line, &Font12, WHITE, BLACK);
    Paint_DrawString_EN(0, 84, bar_line, &Font16, WHITE, BLACK);
    Paint_DrawString_EN(0, 108, beat_line, &Font16, WHITE, BLACK);
}

void draw_playing_page(const UiSnapshot& snapshot, const std::string& layout) {
    if (layout == "compact") {
        Paint_DrawString_EN(0, 0, "PLAY", &Font8, WHITE, BLACK);
        Paint_DrawString_EN(0, 12, snapshot.last_midi.available ? snapshot.last_midi.bytes_hex.c_str() : "NO MIDI", &Font8, WHITE, BLACK);
        return;
    }
    draw_recording_page(snapshot, layout);
}

void draw_midi_page(const UiSnapshot& snapshot, const std::string& layout) {
    if (layout == "compact") {
        char line1[48];
        char line2[48];
        std::snprintf(line1, sizeof(line1), "%s", snapshot.page_title.c_str());
        std::snprintf(line2, sizeof(line2), "%s", snapshot.last_midi.available ? snapshot.last_midi.bytes_hex.c_str() : "NO MIDI");
        Paint_DrawString_EN(0, 0, line1, &Font8, WHITE, BLACK);
        Paint_DrawString_EN(0, 12, line2, &Font8, WHITE, BLACK);
        return;
    }

    char line1[64];
    char line2[64];
    std::snprintf(line1, sizeof(line1), "PORT %d", snapshot.last_midi.global_port);
    std::snprintf(line2, sizeof(line2), "%s", snapshot.last_midi.available ? snapshot.last_midi.bytes_hex.c_str() : "NO MIDI");
    Paint_DrawString_EN(0, 0, snapshot.page_title.c_str(), &Font12, WHITE, BLACK);
    Paint_DrawString_EN(0, 20, line1, &Font16, WHITE, BLACK);
    Paint_DrawString_EN(0, 44, line2, &Font12, WHITE, BLACK);
}

void draw_transport_page(const UiSnapshot& snapshot, const std::string& layout) {
    if (layout == "compact") {
        char line1[48];
        char line2[48];
        std::snprintf(line1, sizeof(line1), "%s", snapshot.page_title.c_str());
        std::snprintf(line2, sizeof(line2), "%s", snapshot.sequencer.reachable ? snapshot.sequencer.summary.c_str() : "SEQ OFF");
        Paint_DrawString_EN(0, 0, line1, &Font8, WHITE, BLACK);
        Paint_DrawString_EN(0, 12, line2, &Font8, WHITE, BLACK);
        return;
    }

    char line1[64];
    char line2[64];
    char line3[64];
    std::snprintf(line1, sizeof(line1), "%s", snapshot.sequencer.reachable ? "Sequencer online" : "Sequencer offline");
    std::snprintf(line2, sizeof(line2), "%s", snapshot.sequencer.summary.c_str());
    std::snprintf(line3, sizeof(line3), "Last MIDI: %s", snapshot.last_midi.available ? snapshot.last_midi.bytes_hex.c_str() : "idle");
    waveshare_display_draw_status(snapshot.page_title.c_str(), line1, line2, line3);
}

void draw_boot_page(const UiSnapshot& snapshot, const std::string& layout) {
    if (draw_monochrome_bmp(snapshot)) {
        spdlog::debug("boot bmp drawn display={} path={}", snapshot.display_id, snapshot.page_image_path);
        return;
    }

    if (layout == "compact") {
        Paint_DrawString_EN(0, 0, "Raptor UI", &Font8, WHITE, BLACK);
        Paint_DrawString_EN(0, 12, snapshot.display_id.c_str(), &Font8, WHITE, BLACK);
        return;
    }

    char line1[64];
    char line2[64];
    char line3[64];
    std::snprintf(line1, sizeof(line1), "Display %s", snapshot.display_id.c_str());
    std::snprintf(line2, sizeof(line2), "%s", snapshot.display_model.c_str());
    std::snprintf(line3, sizeof(line3), "%s", snapshot.sequencer.reachable ? "ready" : "waiting for sequencer");
    waveshare_display_draw_status(snapshot.page_title.c_str(), line1, line2, line3);
}

void draw_page(const UiSnapshot& snapshot, const std::string& layout) {
    if (snapshot.page_type == "boot") {
        draw_boot_page(snapshot, layout);
    } else if (snapshot.page_type == "transport") {
        draw_transport_page(snapshot, layout);
    } else if (snapshot.page_type == "midi_monitor") {
        draw_midi_page(snapshot, layout);
    } else if (snapshot.page_type == "playing") {
        draw_playing_page(snapshot, layout);
    } else if (snapshot.page_type == "recording") {
        draw_recording_page(snapshot, layout);
    } else {
        draw_status_page(snapshot, layout);
    }
}

class WaveshareDisplayBackend final : public DisplayBackend {
public:
    explicit WaveshareDisplayBackend(DisplayConfig config)
        : config_(std::move(config)) {
        descriptor_ = waveshare_display_get_descriptor(config_.model.c_str());
        if (descriptor_ == nullptr) {
            throw std::runtime_error("Unsupported Waveshare display model: " + config_.model);
        }
        effective_layout_ = choose_layout(config_, *descriptor_);
        framebuffer_.resize(framebuffer_bytes(*descriptor_));
    }

    void initialize() override {
        spdlog::debug("display init id={} model={} spi={} speed_hz={}", config_.id, config_.model, config_.hardware.spi_device, config_.hardware.spi_speed_hz);
        spdlog::debug("display init step=apply_hardware id={}", config_.id);
        apply_hardware();
        spdlog::debug("display init step=waveshare_display_initialize begin id={}", config_.id);
        const int rc = waveshare_display_initialize(config_.model.c_str());
        spdlog::debug("display init step=waveshare_display_initialize end id={} rc={}", config_.id, rc);
        if (rc != 0) {
            throw std::runtime_error("Failed to initialize Waveshare display backend");
        }
        spdlog::debug("display init step=clear begin id={}", config_.id);
        clear();
        spdlog::debug("display init step=clear end id={}", config_.id);
    }

    void render(const UiSnapshot& snapshot) override {
        spdlog::trace("display render id={} page_type={}", config_.id, snapshot.page_type);
        apply_hardware();
        if (config_.force_clear_each_frame) {
            spdlog::trace("display render id={} force_clear_each_frame=1", config_.id);
            waveshare_display_clear_panel(config_.model.c_str());
        }
        waveshare_display_prepare_frame(config_.model.c_str(), framebuffer_.data(), config_.rotation);
        const auto resolved_layout = resolve_page_layout(snapshot, effective_layout_);
        draw_page(snapshot, resolved_layout);
        waveshare_display_present(config_.model.c_str(), framebuffer_.data());
    }

    void clear() override {
        apply_hardware();
        std::fill(framebuffer_.begin(), framebuffer_.end(), 0x00);
        waveshare_display_clear_panel(config_.model.c_str());
    }

    std::uint16_t width() const override { return descriptor_->width; }
    std::uint16_t height() const override { return descriptor_->height; }
    const std::string& id() const override { return config_.id; }
    const std::string& model() const override { return config_.model; }
    const std::string& layout() const override { return effective_layout_; }

private:
    void apply_hardware() const {
        const waveshare_hardware_config_t hw = {
            .gpiochip = config_.hardware.gpiochip,
            .spi_device = config_.hardware.spi_device,
            .spi_channel = config_.hardware.spi_channel,
            .spi_speed_hz = config_.hardware.spi_speed_hz,
            .spi_flags = config_.hardware.spi_flags,
            .cs_gpio = config_.hardware.cs_gpio,
            .reset_gpio = config_.hardware.reset_gpio,
            .dc_gpio = config_.hardware.dc_gpio,
        };
        waveshare_display_set_hardware_config(&hw);
    }

    DisplayConfig config_;
    const waveshare_display_descriptor_t* descriptor_ {nullptr};
    std::string effective_layout_;
    std::vector<std::uint8_t> framebuffer_;
};

}  // namespace

std::unique_ptr<DisplayBackend> DisplayBackend::create(const DisplayConfig& config) {
    if (config.driver == "waveshare") {
        return std::make_unique<WaveshareDisplayBackend>(config);
    }
    throw std::runtime_error("Unsupported display driver: " + config.driver);
}

}  // namespace raptor::ui
