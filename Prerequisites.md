Very probably you already checked the pin connections from the matrix panel to the rp2350. Nevertheless some annotations below.

**Hub75rpimini**

My interpretation of your Hub75rpimini PCB.

- Data is delivered via the SPI interface
- You have 5 address lines (A0 to A4) with which 32 rows can be adressed. As the led matrix is split into two parts 64 rows can be displayed. I cannot deduce the width of your matrix panel, so I assume it also has 64 columns. If my assumption is false the width of the panel has to be
adapted, e.g. RGB_MATRIX_WIDTH should be set to the correct number of columns (maybe 32)

        create_hub75_driver(RGB_MATRIX_WIDTH, RGB_MATRIX_HEIGHT);

- R1, G1, B1 and R2, G2, B2 is used for colour data
- Clk, Lat and OE are available

**Prerequisites for the Hub75 Driver**

- The driver implementation  is aimed for a 64x64 led matrix. It can be adapted to a 64x32 matrix panel, a 32x32 matrix panel or another format.

- The pio part requires the RP2350 wiring to be as follows (see hub75.cpp):

        // Wiring of the HUB75 matrix to RP2350
        #define DATA_BASE_PIN    0
        #define DATA_N_PINS      6
        #define ROWSEL_BASE_PIN  6
        #define ROWSEL_N_PINS    5
        #define CLK_PIN          11
        #define STROBE_PIN       12
        #define OEN_PIN          13 

**Some remarks to the wiring**

***Colour Data***

DATA_BASE_PIN is GPIO 0 (start of **consecutive** GPIO pins), so that the colour bits are connected to RP2350 as shown below.

The number (count) of DATA_N_PINS (data pins aka colour pins) is 6.

| colour bit | GPIO |
-------------|-------
| R0         | 0    |
| G0         | 1    |
| B0         | 2    |
| R1         | 3    |
| G1         | 4    |
| B1         | 5    |

***Adress Data***

ROWSEL_BASE_PIN is GPIO 6, that means row addressing starts with GPIO 6.

The number (count) of **consecutive** address pins ROWSEL_N_PINS (A0 - A4) is 5.

The **consecutiveness** of row select pins (address pins) is required by pio (programmable input output).

| address bit | GPIO |
--------------|-------
| A0          | 6    |
| A1          | 7    |
| A2          | 8    |
| A3          | 9    |
| A4          | 10   |

Clock pin is GPIO 11

Strobe (LAT) pin is GPIO 12

Output enable pin (OE) is GPIO 13

**Deviations of Pin Definitions**

Some deviations of this wiring is allowed as long as the address pins and the data pins are **consecutive**. Here an example:

GPIO 15 to GPIO 19 can be used as address pins with the following changes:

        #define ROWSEL_BASE_PIN  15

Colour pins might start at GPIO 3. To achieve this modify the define to

        #define DATA_BASE_PIN    3

Any free GPIO pin might be used for Clock, Latch and OE pins. In this example GPIO 0, GPIO 1 and GPIO 2 are used

        #define CLK_PIN          0
        #define STROBE_PIN       1
        #define OEN_PIN          2

Hier

Hi again,

Thank you for providing information about the pinning.
My pcb is working with interstate library, so I assumed it should work the same with yours.

I am using pimroni as library in my project. I checked out to your branch chained_dma_optimisation.
It is running really well. Frame generation increased from ~300fps to ~425fps.

there is no need for me to overclock rp2350 any more.
I am going to perform overnight stability test to check if it crashes.

Thank you!

Hi Jakub,

I'll keep my fingers crossed 🤞 that the stability test will finish successfull!

The following lines are only suggestions. I thought long and hard about whether to include the following comments in the email at all. But if the proposals seem to be worth it, I gladly support your efforts. I had a look at [festiwalswiatla.hs3.pl](https://festiwalswiatla.hs3.pl/en/) and I a am deeply impressed of your joint work!

**Hub75 Driver**

The [hub75 driver](https://github.com/JuPfu/hub75) might give another performace boost, as it divides the workload between the two pico cores.

```plaintext
+----------------------+       +----------------------+
|        Core 0        |       |        Core 1        |
|                      |       |                      |
|  - pico graphics     |       |  - HUB75 Driver      |
|  - demo effects      |       |                      |
+----------------------+       +----------------------+
```

If you are interested in pursuing this approach I will add initialisation parameters `panel_type` and `inverted_stb` to the hub75 driver.

**panel_type**

If `panel_type equals PANEL_FM6126A` a <b>magic</b> initialisation sequence is sent to the matrix panel.

    void Hub75::FM6126A_setup()
    {
        // Ridiculous register write nonsense for the FM6126A-based 64x64 matrix
        FM6126A_write_register(0b1111111111111110, 12);
        FM6126A_write_register(0b0000001000000000, 13);
    }

This missing enchantment might have caused the problem with your FM6124 matrix panel.

**inverted_stb**

With `ìnverted_stb` set to `True` the hub75 driver will support matrix panels with an inverted OE signal. Just more than a vague assumption this could have been the reason why the RUL6024 matrix panel did not work.

Should those changes prove successful it would be interesting to know if and how much performance gains are achieved. With those changes the gate for using hub75_lvgl will open.

**LVGL**

Without any knowledge of your software it is definitively presumptuous to propose to use [LVGL](https://lvgl.io) for your project. The LVGL library is specifically aimed at microcontrollers. It has a huge wealth of functionality and very good performance. A video [hub75 LVGL](https://www.reddit.com/r/raspberrypipico/comments/1kmegkv/lvgl_on_raspberry_pi_pico_driving_hub75_rgb_led/) shows some capabilities on my led matrix panel. The antialiasing support of the library definitively improved the visual effects and was really helpfull for me.

Best regards
Jürgen

Hi Jakub,

the following branch [panel_type](https://github.com/JuPfu/hub75/tree/panel_type) encloses the changes to (hopefully) support FM6126A based matrix panels. The code is untested as I do not own matrix panels which are affected by the modifications.

I assume that the first step is for you to run this project unchanged to see if the matrices you have prepared show the demo effects implemented here.

To adapt to specific matrix panels set the defines in file hub75_driver.cpp appropriately. Here the standard settings:

                // PanelType - either PANEL_GENERIC or PANEL_FM6126A
                #define PANEL_TYPE PANEL_GENERIC
                // stb_inverted - either true (inverted) or false (default)
                #define STB_INVERTED false

For FM6126 panels change the PANEL_TYPE define as follows:

                // PanelType - either PANEL_GENERIC or PANEL_FM6126A
                #define PANEL_TYPE PANEL_FM6126A
                // stb_inverted - either true (inverted) or false (default)
                #define STB_INVERTED false

For panels with inverted strobe signals change the define as follows:

                // PanelType - either PANEL_GENERIC or PANEL_FM6126A
                #define PANEL_TYPE PANEL_GENERIC
                // stb_inverted - either true (inverted) or false (default)
                #define STB_INVERTED true

If your tests are successful I will merge the changes into the main branch.

The global brightness setting is fixed at the moment. I will have a look at the sources you referenced in your e-mail and see how to implement this functionality.

I am on one week vaccation with my wife starting at Wednesday. I will occasionally look at my e-mails but probably will merely do development.

Best regards
Jürgen


Hi Jakub,

for both projects [hub75](https://github.com/JuPfu/hub75) and [hub75_lvgl](https://github.com/JuPfu/hub75_lvgl) the changes to support FM6126A matrix panel types and matrix panels with inverted strobe signal have been integrated into the main branch. Additionally the CMakeList.txt file should now detect pico boards with and without wifi support and conditionally add **pico_cyw43_arch_none**. The CMakeList.txt file has the board type set to pico2 (this should be RP2350) now.
Therefor you should not have to switch the board type.

I did a test with **#define PANEL_TYPE PANEL_FM6126A** for hub75 and hub75_lvgl. My matrix panel does not need the initialisation sequence and seems to ignore it without a problem.

Is the build process running smooth ? It would be helpful to know which problems you encounter when trying to build the project.

Best regards
Jürgen


row_in_bit_plane = row_address | ((brightness << bit_plane) << 5);


| n | Bitplane | cycles | basis 6u | 
----|----------|--------|-----------
| 1 | 0        | 1      | 6        |
| 1 | 1        | 2      | 12       |
| 1 | 2        | 4      | 24       |
| 1 | 3        | 8      | 48       |
| 1 | 4        | 16     | 96       |
| 1 | 5        | 32     | 192      |
| 1 | 6        | 64     | 384      |
| 1 | 7        | 128    | 768      |
| 1 | 8        | 256    | 1536     |
| 1 | 9        | 512    | 3072     |


Mapping 128 is normal with basis 6u ?
        255 extreme bright  with 9 steps ?
        0   is black with 10 steps ?

brightness is a function 

shift left by additional brightness
shift right by reduced brightness

brightness shift between -10 up to 6
add a base constant to fine grain the brightness. The constant must be chosen to cover the range between to brightness levels
base constant is allowed to be in range from x to y ?

64 is base max
if base is 0 then
   shift right >> 6
   64 is base max


if brightness >= 0 and brightness < 7 then
   (basis << bit_plane << brightness) << 5
else if brightness < 0 and brightness >= -10 then
   (basis << bit_plane >> brightness) << 5


| n | Bitplane | cycles | basis 128u | 
----|----------|--------|-------------
| 1 | 0        | 1      | 128       |
| 1 | 1        | 2      | 256       |
| 1 | 2        | 4      | 512       |
| 1 | 3        | 8      | 1024      |
| 1 | 4        | 16     | 2048      |
| 1 | 5        | 32     | 4096      |
| 1 | 6        | 64     | 8192      |
| 1 | 7        | 128    | 16384     |
| 1 | 8        | 256    | 32768     |
| 1 | 9        | 512    | 65536     |

Hi Jakub,

I hope you've made progress with your project.

Meanwhile I added brightness control for both projects [hub75](https://github.com/JuPfu/hub75) and [hub75_lvgl](https://github.com/JuPfu/hub75_lvgl). See the README.md file in [hub75](https://github.com/JuPfu/hub75) and hub75_driver.cpp for an example. If I had been not clear enough do not hesitate to ask.
I revised the support for FM6126A matrix panel types, too. It would be great if you could let me know that the FM6126A matrix panels are now working.

Best regards
Jürgen

Hello Jakub,

I hope you have made progress with your project.

In the meantime, I have added brightness control for both [hub75](https://github.com/JuPfu/hub75) and [hub75_lvgl](https://github.com/JuPfu/hub75_lvgl) projects. You can find an example in the README.md file in hub75 and hub75_driver.cpp. If I have not been clear enough, please do not hesitate to ask me.

I have also revised the support for FM6126A matrix panel types. It would be great if you could let me know if the FM6126A matrix panels are now working.

Best regards, 
Jürgen

Hi Jakub,

it is nice to hear that brightness control is working 😀!

Here some notes to the current status of both projects (hub75 and hub74_lvgl):
- The gamma correction table has been replaced by a lookup table based on CIE 1931 definitions to drive luminance.
  [The CIE 1931 lightness formula is what actually describes how we perceive light](https://jared.geek.nz/2013/02/linear-led-pwm/).
  You can switch back to the gamma correction table by commenting out or removing the line `#define CIE_LUT` if you like.
- Scan rate support (at least for multiplexing 2 (default) and 4 rows) is implemented. As I do not own an appropriate matrix panel
  this feature is untested. See the README.md file for more information.

I am curious if FM6126A matrix panels are working now and if multiplexing of 4 rows is also 

I hope you have made progress with your project. By the way lvgl does support [jpg](https://docs.lvgl.io/master/details/libs/libjpeg_turbo.html) and [png](https://docs.lvgl.io/master/details/libs/libpng.html) files. The images should have been scaled on the server side before transfer to the pico. I can not estimate which frame rate can be achieved when you go with this proposal neither can I
judge if this is even a suitable way for you to go.

Best regards, 
Jürgen