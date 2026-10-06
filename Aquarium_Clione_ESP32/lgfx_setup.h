#ifndef PIXAQ_LGFX_SETUP_H
#define PIXAQ_LGFX_SETUP_H
// ---------------------------------------------------------------------------
// LovyanGFX configuration for the 320x240 ST7789 panel.
// XIAO ESP32C3: SCLK D8/GPIO8, MOSI D10/GPIO10, DC D2/GPIO4, CS D3/GPIO5.
// ESP32-S3 DevKit fallback wiring (same as Aquarium_Espresso):
//   SCLK 12 / MOSI 11 / DC 9 / CS 10 / RST -- (tie to 3V3 or set PIN_TFT_RST)
// Panel options: BGR order + inversion on, as on the other ST7789 boards here.
// ---------------------------------------------------------------------------
#define LGFX_USE_V1
// Memory-only image sources: match the core library class definitions.
// This avoids the include-order filesystem wrapper and LTO type mismatches.
// First dependency-discovery pass has not added the library include path yet.
#if __has_include(<lgfx/v1/LGFXBase.hpp>)
#include <lgfx/v1/LGFXBase.hpp>
#endif
#include <LovyanGFX.hpp>

// XIAO ESP32C3: D8=SCK, D10=MOSI, D2=DC, D3=CS.
#ifndef PIN_TFT_SCLK
#if CONFIG_IDF_TARGET_ESP32C3
#define PIN_TFT_SCLK 8
#else
#define PIN_TFT_SCLK 12
#endif
#endif
#ifndef PIN_TFT_MOSI
#if CONFIG_IDF_TARGET_ESP32C3
#define PIN_TFT_MOSI 10
#else
#define PIN_TFT_MOSI 11
#endif
#endif
#ifndef PIN_TFT_DC
#if CONFIG_IDF_TARGET_ESP32C3
#define PIN_TFT_DC 4
#else
#define PIN_TFT_DC 9
#endif
#endif
#ifndef PIN_TFT_CS
#if CONFIG_IDF_TARGET_ESP32C3
#define PIN_TFT_CS 5
#else
#define PIN_TFT_CS 10
#endif
#endif
#ifndef PIN_TFT_RST
#if CONFIG_IDF_TARGET_ESP32C3
#define PIN_TFT_RST 3 // XIAO D1: LCD reset controlled by LovyanGFX.
#else
#define PIN_TFT_RST -1 // S3: tie RST to 3V3, or override with a free GPIO.
#endif
#endif
#ifndef PIN_TFT_BLK
#define PIN_TFT_BLK -1 // Tie BLK to 3V3.
#endif
#ifndef TFT_SPI_FREQ
#if CONFIG_IDF_TARGET_ESP32C3
#define TFT_SPI_FREQ 80000000 // XIAO C3 runs fine at 80 MHz; halves the per-band transfer time.
#else
#define TFT_SPI_FREQ 40000000 // S3: same clock as the 1.0.0 release build.
#endif
#endif

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;

public:
    LGFX()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = TFT_SPI_FREQ;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_TFT_SCLK;
            cfg.pin_mosi = PIN_TFT_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc   = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = PIN_TFT_CS;
            cfg.pin_rst  = PIN_TFT_RST;
            cfg.pin_busy = -1;
            cfg.panel_width     = 240;   // portrait native; setRotation(1) -> 320x240
            cfg.panel_height    = 320;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = false;
            cfg.invert          = true;
            cfg.rgb_order       = false; // BGR
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

#endif // PIXAQ_LGFX_SETUP_H
