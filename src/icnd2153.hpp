#define HIGH 1
#define LOW 0

static void icnd2153_send_command(uint dclk_pin, uint le_pin, uint n_edges);
static void icnd2153_startup_sequence(uint dclk_pin, uint le_pin);
static void icnd2153_start_gclk(uint gclk_pin, float freq_hz);

// ====
static inline void pulse_clk();
static inline void pulse_lat();
static inline void shift_rgb6(uint8_t value);
static void icnd2153_write_register(uint16_t reg);
void icnd2153_init();