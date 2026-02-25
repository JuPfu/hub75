// Example derived from https://github.com/pimoroni/pimoroni-pico/blob/main/examples/interstate75/interstate75_fire_effect.cpp
#include "fire_effect.hpp"

// Display size in pixels
// Should be either 64x64 or 32x32 but perhaps 64x32 an other sizes will work.
// Note: this example uses only 5 address lines so it's limited to 32*2 pixels.

void FireEffect::burn()
{
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      float value = get(x, y);

      if (value > 0.5f)
      {
        int r = 25 - (int)((255 * value) * 0.1f);
        set_pen(255 - r, 255 - r, (int)(150 * value) + 105);
      }
      else if (value > 0.4f)
      {
        int b = (int)(350 * value) - 140;
        set_pen(220 + (b >> 1), 160, b);
      }
      else if (value > 0.3f)
      {
        int b = (int)(500 * value) - 150;
        set_pen(180 + (b >> 1), 30, b);
      }
      else
      {
        int c = (int)(150 * value);
        set_pen(c, c, c);
      }

      pixel(Point(x, y));

      // update this pixel by averaging the below pixels
      float average = (get(x, y) + get(x, y + 2) + get(x, y + 1) + get(x - 1, y + 1) + get(x + 1, y + 1)) / 5.0f;

      // damping factor to ensure flame tapers out towards the top of the displays
      average *= landscape ? 0.985f : 0.99f;

      // update the heat map with our newly averaged value
      set(x, y, average);
    }
  }

  // clear the bottom row and then add a new fire seed to it
  for (int x = 0; x < width; x++)
  {
    set(x, height - 1, 0.0f);
  }

  // add a new random heat source
  int source_count = landscape ? 7 : 1;
  for (int c = 0; c < source_count; c++)
  {
    int px = (rand() % (width - 4)) + 2;
    set(px, height - 2, 1.0f);
    set(px + 1, height - 2, 1.0f);
    set(px - 1, height - 2, 1.0f);
    set(px, height - 1, 1.0f);
    set(px + 1, height - 1, 1.0f);
    set(px - 1, height - 1, 1.0f);
  }
}
