#pragma once
#include <stdint.h>

// Select with the build tool. Keep board wiring out of BLE/UPS and UI code.
#define BOARD_WAVESHARE_147 1
#define BOARD_POCKET_096 2
#ifndef ADAPTER_BOARD
#define ADAPTER_BOARD BOARD_WAVESHARE_147
#endif
namespace Board {
enum class PanelKind {ST7789,ST7735};
constexpr int boot=0,flashMiB=16,psramMiB=8;
#if ADAPTER_BOARD == BOARD_WAVESHARE_147
constexpr const char *id="waveshare-147",*panelName="ST7789";
constexpr PanelKind panel=PanelKind::ST7789;
constexpr int width=172,height=320,mosi=45,clock=40,cs=42,dc=41,reset=39;
constexpr int backlight=48,rgb=38,xOffset=34,yOffset=0;
constexpr bool compact=false,backlightHigh=true,littleEndian=true;
constexpr uint8_t screenCount=3;
#elif ADAPTER_BOARD == BOARD_POCKET_096
// Pocket-Dongle-S3 schematic 2025-04-24 and vendor GIF TFT_eSPI setup.
// Backlight cathode is wired through 10 ohms to GND: no GPIO control.
// No addressable RGB LED in this schematic. GPIO38 must stay untouched.
constexpr const char *id="pocket-096",*panelName="ST7735S";
constexpr PanelKind panel=PanelKind::ST7735;
constexpr int width=80,height=160,mosi=11,clock=10,cs=12,dc=13,reset=14;
constexpr int backlight=-1,rgb=-1,xOffset=26,yOffset=1;
constexpr bool compact=true,backlightHigh=true,littleEndian=false;
constexpr uint8_t screenCount=6; // dashboard, graph, four diagnostic pages
#else
#error Unknown ADAPTER_BOARD
#endif
static_assert(boot!=cs&&boot!=dc&&boot!=reset,"BOOT must remain an input");
static_assert(rgb<0||rgb!=backlight,"RGB and backlight must not share a pin");
}
