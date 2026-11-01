#pragma once
#include <stdint.h>

namespace PanelWindow {
// Called within one selected SPI transaction; D/C selects each command/data
// byte. Both ST77xx controllers accept consecutive bytes while CS remains low.
template <class Command>
void address(unsigned left, unsigned top, unsigned width, unsigned height, unsigned xOffset,
             unsigned yOffset, Command command) {
  const uint16_t x0 = uint16_t(xOffset + left), x1 = uint16_t(x0 + width - 1);
  const uint16_t y0 = uint16_t(yOffset + top), y1 = uint16_t(y0 + height - 1);
  alignas(uint32_t)
      uint8_t data[4] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
  command(0x2a, data, 4);
  data[0] = uint8_t(y0 >> 8);
  data[1] = uint8_t(y0);
  data[2] = uint8_t(y1 >> 8);
  data[3] = uint8_t(y1);
  command(0x2b, data, 4);
  command(0x2c, nullptr, 0);
}
} // namespace PanelWindow
