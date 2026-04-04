#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* model_key;
    uint16_t width;
    uint16_t height;
    uint8_t scale;
    uint16_t rotation;
} waveshare_display_descriptor_t;

typedef struct {
    int gpiochip;
    int spi_device;
    int spi_channel;
    int spi_speed_hz;
    int spi_flags;
    int cs_gpio;
    int reset_gpio;
    int dc_gpio;
} waveshare_hardware_config_t;

const waveshare_display_descriptor_t* waveshare_display_get_descriptor(const char* model_key);
void waveshare_display_set_hardware_config(const waveshare_hardware_config_t* config);
int waveshare_display_initialize(const char* model_key);
void waveshare_display_clear_panel(const char* model_key);
void waveshare_display_prepare_frame(const char* model_key, uint8_t* image, uint16_t rotation);
void waveshare_display_draw_status(const char* title, const char* line1, const char* line2, const char* line3);
void waveshare_display_present(const char* model_key, uint8_t* image);

#ifdef __cplusplus
}
#endif
