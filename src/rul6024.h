// WREG1 components
//
// Name         Bit R1  Default  Description
// R_UP         15:11            White Balance Adjustment Register
//                               VR_UP=VDD-3.25V+ <15:11>*0.0855V
//                      0b00111
#define R_UP 0b00111 << 11
//
// R_IGAIN      10:07            Current Gain Selection
//                               0000~0110：IOUT=IOUT*(25%+<10:7>*3.125%)
//                               0111~1111：IOUT=IOUT*(50%+(<10:7>-7)*6.25%)
//                      0b0011
#define R_IGAIN 0b0011 << 7
//
// R_V0P3       06:04            Constant Current Source Inflection Point Base Voltage Selection
//                               V0P3=0.19V+ <6:4>*0.03V
//                      0b011
//
#define R_V0P3 0b011 << 4
//
// R_V1P26      03:00            Constant current source current fine-tuning register, a signed number:
//                               <3>=1：IOUT=IOUT*(1+<2:0>*0.4%)
//                               <3>=0：IOUT=IOUT*(1-<2:0>*0.4%)
//                      0b0100
#define R_V1P26 0b0100

#define WREG1 R_UP | R_IGAIN | R_V0P3 | R_V1P26

// WREG2 components
//
// Name         Bit R1  Default  Description
// Reserved     15:11            Reserved
// R_OE_CH      10:10            In the time-sharing display scheme, channel OE signal selection
//                               0: OEN falling edge starts to turn on
//                               1: OEN rising edge starts to open
//                      0b1
#define R_OE_CH 0b1 << 10
//
// R_TDM        09:09            Time-sharing display function enable signal
//                               0：disable
//                               1：enable
//                      0b0
#define R_TDM 0b1 << 9
//
// R_UPCTRL     08:08            Cancellation circuit enable signal selection
//                               0: Program control erasure (erasure at line break)
//                               1: Register ROUT2<3> controls the pull-up
//                      0b1  ???
#define R_UPCTRL 0b0 << 8
//
// R_FALL_TIME  07:07            Channel output falling edge time selection
//                               0：35ns
//                               1：55ns
//                      0b0
#define R_FALL_TIME 0b0 << 7
//
// R_LATCH      06:06            LATCH method selection
//                               0: When LE is less than 3CLK width, the output channel does not latch data;
//                                  when LE is greater than or equal to 3CLK
//                                  When the width is set, data is latched at the falling edge of LE
//                               1: LE falling edge latches
//                      0b1
#define R_LATCH 0b1 << 6
//
// R_UPCH       05:05            0: The pull-up and pull-down of the blanking function work at the same time
//                               1: The pull-up and pull-down of the shadow elimination function are independent.
//                               When there is no data, only pull-up is performed, and there is no pull-down path;
//                               when there is data, pull-up and pull-down work at the same time.
//                      0b1 ???
#define R_UPCH 0b0 << 5
//
// R_EN_AM      04:04            Internal debugging use
//                      0b1 ???
#define R_EN_AM 0b0 << 4
//
// ROUT2<3>     03:03            When register R_UPCTRL=1:
//                               0: Disable the shadow elimination function
//                               1: Register R_UP control the shadow elimination
//                      0b1 ???
#define ROUT2 0b1 << 3
//
// R_CLK_SDO    02:02            CLK to SDO delay selection
//                               0：20n
//                               1：13ns
//                      0b1 ???
#define R_CLK_SDO 0b1 << 2
//
// R_OE         01:00            OE extension width selection
//                               00：0ns
//                               01：10ns
//                               11: 20ns
//                       0b00
#define R_OE 0b01

#define WREG2 R_OE_CH | R_TDM | R_UPCTRL | R_FALL_TIME | R_LATCH | R_UPCH | R_EN_AM | ROUT2 | R_CLK_SDO | R_OE

#define HIGH 1
#define LOW 0

#define CMD_RESET_OEN 1
#define CMD_DATA_LATCH 3
#define CMD_WREG1 11
#define CMD_WREG2 12

void RUL6024_setup();
void RUL6024_write_command(uint8_t command);
void RUL6024_write_register(uint16_t value, uint8_t position);
void RUL6024_init_register();
