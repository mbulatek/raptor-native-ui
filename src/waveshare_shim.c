#include "raptor_ui/waveshare_shim.h"

#include "DEV_Config.h"
#include "GUI_Paint.h"
#include "OLED_0in91.h"
#include "OLED_1in3.h"
#include "OLED_1in32.h"
#include "OLED_1in5.h"
#include "OLED_1in5_b.h"
#include "OLED_1in51.h"
#include "OLED_1in54.h"
#include "OLED_2in42.h"

#include <string.h>

static const waveshare_display_descriptor_t g_displays[] = {
    {"oled_0in91", 128, 32, 2, 0},
    {"oled_1in3", OLED_1IN3_WIDTH, OLED_1IN3_HEIGHT, 2, 90},
    {"oled_1in32", OLED_1in32_WIDTH, OLED_1in32_HEIGHT, 16, 0},
    {"oled_1in5", OLED_1in5_WIDTH, OLED_1in5_HEIGHT, 16, 0},
    {"oled_1in5_b", OLED_1in5_B_WIDTH, OLED_1in5_B_HEIGHT, 2, 0},
    {"oled_1in51", OLED_1in51_WIDTH, OLED_1in51_HEIGHT, 16, 0},
    {"oled_1in54", OLED_1in54_WIDTH, OLED_1in54_HEIGHT, 16, 0},
    {"oled_2in42", OLED_2IN42_WIDTH, OLED_2IN42_HEIGHT, 2, 270},
};

static const waveshare_display_descriptor_t* lookup(const char* model_key) {
    size_t count = sizeof(g_displays) / sizeof(g_displays[0]);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(g_displays[i].model_key, model_key) == 0) {
            return &g_displays[i];
        }
    }
    return NULL;
}

static int ensure_module_ready(void) {
    /* Vendor API returns 0 on success, non-zero on failure. We must not ignore
       failures, otherwise OLED init/clear/display may run with half-initialized
       GPIO/SPI and crash. */
    return DEV_ModuleInit() == 0 ? 0 : -1;
}

const waveshare_display_descriptor_t* waveshare_display_get_descriptor(const char* model_key) {
    return lookup(model_key);
}

void waveshare_display_set_hardware_config(const waveshare_hardware_config_t* config) {
    waveshare_dev_hardware_config_t hw = {
        .gpiochip = 0,
        .spi_device = 0,
        .spi_channel = 0,
        .spi_speed_hz = 10000000,
        .spi_flags = 3,  // SPI mode 3 (matches vendor examples)
        .cs_gpio = 8,
        .reset_gpio = 27,
        .dc_gpio = 25,
    };

    if (config != NULL) {
        hw.gpiochip = config->gpiochip;
        hw.spi_device = config->spi_device;
        hw.spi_channel = config->spi_channel;
        hw.spi_speed_hz = config->spi_speed_hz;
        hw.spi_flags = config->spi_flags;
        hw.cs_gpio = config->cs_gpio;
        hw.reset_gpio = config->reset_gpio;
        hw.dc_gpio = config->dc_gpio;
    }

    waveshare_dev_config_set_hardware(&hw);
}

int waveshare_display_initialize(const char* model_key) {
    if (ensure_module_ready() != 0) {
        return -1;
    }

    if (strcmp(model_key, "oled_0in91") == 0) {
        OLED_0in91_Init();
    } else if (strcmp(model_key, "oled_1in3") == 0) {
        OLED_1IN3_Init();
    } else if (strcmp(model_key, "oled_1in32") == 0) {
        OLED_1in32_Init();
    } else if (strcmp(model_key, "oled_1in5") == 0) {
        OLED_1in5_Init();
    } else if (strcmp(model_key, "oled_1in5_b") == 0) {
        OLED_1in5_B_Init();
    } else if (strcmp(model_key, "oled_1in51") == 0) {
        OLED_1in51_Init();
    } else if (strcmp(model_key, "oled_1in54") == 0) {
        OLED_1in54_Init();
    } else if (strcmp(model_key, "oled_2in42") == 0) {
        OLED_2in42_Init();
    } else {
        return -1;
    }

    return 0;
}

void waveshare_display_clear_panel(const char* model_key) {
    if (ensure_module_ready() != 0) {
        return;
    }
    if (strcmp(model_key, "oled_0in91") == 0) {
        OLED_0in91_Clear();
    } else if (strcmp(model_key, "oled_1in3") == 0) {
        OLED_1IN3_Clear();
    } else if (strcmp(model_key, "oled_1in32") == 0) {
        OLED_1in32_Clear();
    } else if (strcmp(model_key, "oled_1in5") == 0) {
        OLED_1in5_Clear();
    } else if (strcmp(model_key, "oled_1in5_b") == 0) {
        OLED_1in5_B_Clear();
    } else if (strcmp(model_key, "oled_1in51") == 0) {
        OLED_1in51_Clear();
    } else if (strcmp(model_key, "oled_1in54") == 0) {
        OLED_1in54_Clear();
    } else if (strcmp(model_key, "oled_2in42") == 0) {
        OLED_2in42_Clear();
    }
}

void waveshare_display_prepare_frame(const char* model_key, uint8_t* image, uint16_t rotation) {
    const waveshare_display_descriptor_t* descriptor = lookup(model_key);
    if (descriptor == NULL) {
        return;
    }

    const uint16_t effective_rotation = rotation == 0 ? descriptor->rotation : rotation;
    Paint_NewImage(image, descriptor->width, descriptor->height, effective_rotation, BLACK);
    Paint_SetScale(descriptor->scale);
    Paint_SelectImage(image);
    Paint_Clear(BLACK);
}

void waveshare_display_draw_status(const char* title, const char* line1, const char* line2, const char* line3) {
    Paint_DrawString_EN(4, 4, title, &Font16, WHITE, BLACK);
    Paint_DrawLine(0, 22, Paint.Width - 1, 22, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawString_EN(4, 28, line1, &Font12, WHITE, BLACK);
    Paint_DrawString_EN(4, 46, line2, &Font12, WHITE, BLACK);
    Paint_DrawString_EN(4, 64, line3, &Font12, WHITE, BLACK);
}

void waveshare_display_present(const char* model_key, uint8_t* image) {
    if (ensure_module_ready() != 0) {
        return;
    }
    if (strcmp(model_key, "oled_0in91") == 0) {
        OLED_0in91_Display(image);
    } else if (strcmp(model_key, "oled_1in3") == 0) {
        OLED_1IN3_Display(image);
    } else if (strcmp(model_key, "oled_1in32") == 0) {
        OLED_1in32_Display(image);
    } else if (strcmp(model_key, "oled_1in5") == 0) {
        OLED_1in5_Display(image);
    } else if (strcmp(model_key, "oled_1in5_b") == 0) {
        OLED_1in5_B_Display(image);
    } else if (strcmp(model_key, "oled_1in51") == 0) {
        OLED_1in51_Display(image);
    } else if (strcmp(model_key, "oled_1in54") == 0) {
        OLED_1in54_Display(image);
    } else if (strcmp(model_key, "oled_2in42") == 0) {
        OLED_2in42_Display(image);
    }
}
