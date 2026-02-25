#define HIGH 1
#define LOW 0

void FM6126A_init_register();
void FM6126A_write_register(uint16_t value, uint8_t position);
void FM6126A_setup();