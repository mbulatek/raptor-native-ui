#define _POSIX_C_SOURCE 200809L
/*
 * Raptor-customized Waveshare DEV_Config implementation.
 *
 * The original vendor backend was replaced with a libgpiod + spidev transport
 * while preserving the DEV_* API expected by the copied Waveshare sources.
 */
#include "DEV_Config.h"

#include <errno.h>
#include <stdio.h>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static waveshare_dev_hardware_config_t g_config = {
    .gpiochip = 0,
    .spi_device = 0,
    .spi_channel = 0,
    .spi_speed_hz = 10000000,
    .spi_flags = 0,
    .cs_gpio = 8,
    .reset_gpio = 27,
    .dc_gpio = 25,
};

static waveshare_dev_hardware_config_t g_active_config = {
    .gpiochip = -1,
    .spi_device = -1,
    .spi_channel = -1,
    .spi_speed_hz = -1,
    .spi_flags = -1,
    .cs_gpio = -1,
    .reset_gpio = -1,
    .dc_gpio = -1,
};

struct requested_line {
    unsigned int offset;
    struct gpiod_line_request* request;
};

static struct gpiod_chip* g_gpio_chip = NULL;
static struct requested_line g_cs_line = {0, NULL};
static struct requested_line g_rst_line = {0, NULL};
static struct requested_line g_dc_line = {0, NULL};
static int g_spi_fd = -1;

static bool config_equals(const waveshare_dev_hardware_config_t* lhs,
                          const waveshare_dev_hardware_config_t* rhs) {
    return lhs->gpiochip == rhs->gpiochip &&
           lhs->spi_device == rhs->spi_device &&
           lhs->spi_channel == rhs->spi_channel &&
           lhs->spi_speed_hz == rhs->spi_speed_hz &&
           lhs->spi_flags == rhs->spi_flags &&
           lhs->cs_gpio == rhs->cs_gpio &&
           lhs->reset_gpio == rhs->reset_gpio &&
           lhs->dc_gpio == rhs->dc_gpio;
}

static struct requested_line* line_for_pin(UWORD Pin) {
    if (Pin == (UWORD)g_config.cs_gpio) {
        return &g_cs_line;
    }
    if (Pin == (UWORD)g_config.reset_gpio) {
        return &g_rst_line;
    }
    if (Pin == (UWORD)g_config.dc_gpio) {
        return &g_dc_line;
    }
    return NULL;
}

static void release_requested_line(struct requested_line* line) {
    if (line != NULL && line->request != NULL) {
        gpiod_line_request_release(line->request);
        line->request = NULL;
    }
}

static void release_line_for_pin(UWORD Pin) {
    release_requested_line(line_for_pin(Pin));
}

static int request_line(struct requested_line* line,
                        unsigned int pin,
                        const char* consumer,
                        enum gpiod_line_direction direction,
                        enum gpiod_line_value output_value) {
    struct gpiod_line_settings* settings = NULL;
    struct gpiod_line_config* line_config = NULL;
    struct gpiod_request_config* request_config = NULL;
    struct gpiod_line_request* request = NULL;
    const unsigned int offsets[] = {pin};
    int rc = -1;

    if (g_gpio_chip == NULL || line == NULL) {
        return -1;
    }

    settings = gpiod_line_settings_new();
    line_config = gpiod_line_config_new();
    request_config = gpiod_request_config_new();
    if (settings == NULL || line_config == NULL || request_config == NULL) {
        goto cleanup;
    }

    if (gpiod_line_settings_set_direction(settings, direction) < 0) {
        goto cleanup;
    }
    if (direction == GPIOD_LINE_DIRECTION_OUTPUT &&
        gpiod_line_settings_set_output_value(settings, output_value) < 0) {
        goto cleanup;
    }
    if (gpiod_line_config_add_line_settings(line_config, offsets, 1, settings) < 0) {
        goto cleanup;
    }
    gpiod_request_config_set_consumer(request_config, consumer);

    request = gpiod_chip_request_lines(g_gpio_chip, request_config, line_config);
    if (request == NULL) {
        fprintf(stderr, "gpiod_chip_request_lines consumer=%s gpiochip=%d pin=%u failed: %s\n",
                consumer, g_config.gpiochip, pin, strerror(errno));
        goto cleanup;
    }

    release_requested_line(line);
    line->offset = pin;
    line->request = request;
    request = NULL;
    rc = 0;

cleanup:
    if (request != NULL) {
        gpiod_line_request_release(request);
    }
    if (request_config != NULL) {
        gpiod_request_config_free(request_config);
    }
    if (line_config != NULL) {
        gpiod_line_config_free(line_config);
    }
    if (settings != NULL) {
        gpiod_line_settings_free(settings);
    }
    return rc;
}

void waveshare_dev_config_set_hardware(const waveshare_dev_hardware_config_t* config) {
    if (config == NULL) {
        return;
    }
    if ((g_gpio_chip != NULL || g_spi_fd >= 0) && !config_equals(&g_config, config)) {
        DEV_ModuleExit();
    }
    g_config = *config;
}

UWORD waveshare_dev_config_cs_pin(void) { return (UWORD)g_config.cs_gpio; }
UWORD waveshare_dev_config_reset_pin(void) { return (UWORD)g_config.reset_gpio; }
UWORD waveshare_dev_config_dc_pin(void) { return (UWORD)g_config.dc_gpio; }

void DEV_Digital_Write(UWORD Pin, UBYTE Value) {
    struct requested_line* line = line_for_pin(Pin);
    if (line != NULL && line->request != NULL) {
        gpiod_line_request_set_value(line->request, line->offset,
                                     Value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    }
}

UBYTE DEV_Digital_Read(UWORD Pin) {
    struct requested_line* line = line_for_pin(Pin);
    enum gpiod_line_value value;

    if (line == NULL || line->request == NULL) {
        return 0;
    }

    value = gpiod_line_request_get_value(line->request, line->offset);
    if (value == GPIOD_LINE_VALUE_ERROR) {
        return 0;
    }

    return value == GPIOD_LINE_VALUE_ACTIVE ? 1 : 0;
}

void DEV_GPIO_Mode(UWORD Pin, UWORD Mode) {
    struct requested_line* line = line_for_pin(Pin);
    if (g_gpio_chip == NULL || line == NULL) {
        return;
    }

    release_line_for_pin(Pin);
    if (Mode == 0) {
        (void)request_line(line, line->offset ? line->offset : (unsigned int)Pin,
                           "raptor-ui-input", GPIOD_LINE_DIRECTION_INPUT,
                           GPIOD_LINE_VALUE_INACTIVE);
    } else {
        (void)request_line(line, line->offset ? line->offset : (unsigned int)Pin,
                           "raptor-ui-output", GPIOD_LINE_DIRECTION_OUTPUT,
                           GPIOD_LINE_VALUE_INACTIVE);
    }
}

void DEV_Delay_ms(UDOUBLE xms) {
    struct timespec ts;
    ts.tv_sec = xms / 1000U;
    ts.tv_nsec = (long)((xms % 1000U) * 1000000UL);
    (void)nanosleep(&ts, NULL);
}

static int DEV_GPIO_Init(void) {
    if (request_line(&g_cs_line, (unsigned int)g_config.cs_gpio, "raptor-ui-cs",
                     GPIOD_LINE_DIRECTION_OUTPUT, GPIOD_LINE_VALUE_ACTIVE) < 0) {
        fprintf(stderr, "GPIO init failed: cs_gpio=%d gpiochip=%d\n", g_config.cs_gpio, g_config.gpiochip);
        return -1;
    }
    if (request_line(&g_rst_line, (unsigned int)g_config.reset_gpio, "raptor-ui-rst",
                     GPIOD_LINE_DIRECTION_OUTPUT, GPIOD_LINE_VALUE_ACTIVE) < 0) {
        fprintf(stderr, "GPIO init failed: reset_gpio=%d gpiochip=%d\n", g_config.reset_gpio, g_config.gpiochip);
        return -1;
    }
    if (request_line(&g_dc_line, (unsigned int)g_config.dc_gpio, "raptor-ui-dc",
                     GPIOD_LINE_DIRECTION_OUTPUT, GPIOD_LINE_VALUE_ACTIVE) < 0) {
        fprintf(stderr, "GPIO init failed: dc_gpio=%d gpiochip=%d\n", g_config.dc_gpio, g_config.gpiochip);
        return -1;
    }
    return 0;
}

static int DEV_SPI_Init(void) {
    char spidev_path[64];
    snprintf(spidev_path, sizeof(spidev_path), "/dev/spidev%d.%d", g_config.spi_device, g_config.spi_channel);

    g_spi_fd = open(spidev_path, O_RDWR);
    if (g_spi_fd < 0) {
        fprintf(stderr, "open(%s) failed\n", spidev_path);
        return -1;
    }

    uint8_t mode = (uint8_t)(g_config.spi_flags & 0x3);
    /* soft-cs: when cs_gpio is configured we disable hardware CS and toggle the GPIO manually */
    if (g_config.cs_gpio >= 0) {
        mode = (uint8_t)(mode | SPI_NO_CS);
    }
    uint8_t bits_per_word = 8;
    uint32_t speed_hz = (uint32_t)g_config.spi_speed_hz;

    if (ioctl(g_spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0 ||
        ioctl(g_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) {
        fprintf(stderr, "spidev ioctl configuration failed\n");
        close(g_spi_fd);
        g_spi_fd = -1;
        return -1;
    }

    return 0;
}

UBYTE DEV_ModuleInit(void) {
    char gpiochip_path[64];

    if (g_gpio_chip != NULL && g_spi_fd >= 0 && config_equals(&g_active_config, &g_config)) {
        return 0;
    }

    DEV_ModuleExit();

    snprintf(gpiochip_path, sizeof(gpiochip_path), "/dev/gpiochip%d", g_config.gpiochip);
    g_gpio_chip = gpiod_chip_open(gpiochip_path);
    if (g_gpio_chip == NULL) {
        fprintf(stderr, "gpiod_chip_open(%s) failed\n", gpiochip_path);
        return 1;
    }

    if (DEV_GPIO_Init() < 0) {
        fprintf(stderr, "libgpiod GPIO init failed\n");
        DEV_ModuleExit();
        return 1;
    }

    if (DEV_SPI_Init() < 0) {
        DEV_ModuleExit();
        return 1;
    }

    g_active_config = g_config;
    OLED_CS_1;
    OLED_RST_1;
    OLED_DC_1;
    return 0;
}

void DEV_SPI_WriteByte(UBYTE Value) {
    DEV_SPI_Write_nByte(&Value, 1);
}

void DEV_SPI_Write_nByte(uint8_t* pData, uint32_t Len) {
    if (g_spi_fd < 0 || pData == NULL || Len == 0) {
        return;
    }

    struct spi_ioc_transfer transfer;
    memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = (unsigned long)pData;
    transfer.len = Len;
    transfer.speed_hz = (uint32_t)g_config.spi_speed_hz;
    transfer.bits_per_word = 8;

        /* soft-cs: assert before transfer */
    if (g_gpio_chip != NULL && g_cs_line.request != NULL) {
        OLED_CS_0;
    }

    (void)ioctl(g_spi_fd, SPI_IOC_MESSAGE(1), &transfer);

    /* soft-cs: deassert after transfer */
    if (g_gpio_chip != NULL && g_cs_line.request != NULL) {
        OLED_CS_1;
    }
}

void I2C_Write_Byte(uint8_t value, uint8_t Cmd) {
    (void)value;
    (void)Cmd;
}

void DEV_ModuleExit(void) {
    if (g_spi_fd >= 0) {
        close(g_spi_fd);
        g_spi_fd = -1;
    }

    release_requested_line(&g_cs_line);
    release_requested_line(&g_rst_line);
    release_requested_line(&g_dc_line);

    if (g_gpio_chip != NULL) {
        gpiod_chip_close(g_gpio_chip);
        g_gpio_chip = NULL;
    }
}