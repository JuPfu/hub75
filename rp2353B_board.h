// rp2350b_board.h
// Custom board definition for RP2350B (QFN-80, 48 GPIOs)

#ifndef _BOARDS_RP2350B_BOARD_H
#define _BOARDS_RP2350B_BOARD_H

// ----- RP2350 VARIANT -----
// 0 = RP2350B (QFN-80, 48 GPIOs)
// 1 = RP2350A (QFN-60, 30 GPIOs)
#define PICO_RP2350A 0

// ----- Flash -----
// Adjust to match your actual flash chip (e.g. 4MB, 8MB, 16MB)
#define PICO_FLASH_SIZE_BYTES (8 * 1024 * 1024)
#define PICO_FLASH_SPI_CLKDIV 2

// ----- Board name -----
#define PICO_BOARD "rp2350b_board"

// ----- Default UART -----
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// ----- Default LED (optional, adjust pin to match your board) -----
// #define PICO_DEFAULT_LED_PIN 25

// ----- Default SPI (optional) -----
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// ----- Default I2C (optional) -----
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

#endif // _BOARDS_RP2350B_BOARD_H
