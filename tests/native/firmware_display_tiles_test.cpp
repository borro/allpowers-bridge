#include <array>
#include <cassert>
#include <initializer_list>
#include <vector>
#include <algorithm>
#include "DisplayTiles.h"
#include "PanelWindow.h"
#include "display_tiles_test.h"

int main() {
  DisplayTilesTests::run<172,320,true>();
  DisplayTilesTests::run<80,160,false>();
  DisplayTilesTests::run<5,19,true>();
  DisplayTilesTests::run<5,19,false>();
  DisplayTilesTests::highAddresses();
}
