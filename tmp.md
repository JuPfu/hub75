What I like about the changes:

1. vast reduction of "defines" and preprocessor macros
2. compile time checks done via Template (e.g. constexpr)
3. move from C to C++ to ease instantiation of multiple driver instances
4. The management of PIOs and IRQs has changed fundamentally
  I have to figure out claiming and unclaiming of PIOs and SMs
  I have to check global irq management

What has to be done

1. cie.py has to be adapted
   

Questions:

Does the Pico C-SDK support CCMAKE_CXX_STANDARD 20 ?



hub75.tpp
  update ✅
  update_bgr ✅
    check for "if constexpr (Cfg.panel.chain_cols == 1 && Cfg.panel.chain_rows == 1)" as t had been dropped in main-branch
  map_panel_row ✅
  rot_lut ✅
  rot_lut_rgb ✅
  rotated_src_index ✅
  pack_lut_rgb_ ✅
  pack_lut_rgb ✅


  apply_ccm  move from hub75.hpp macro definition to method apply_ccm in hub75.tpp - visual check CLAMP substituted by check for maxval
  // Colour pipeline
  cie_red_table
  cie_reen_table
  cie_blue_table    deduced from macro definitions in hub75.hpp

  setup_dma_transfers ✅

  hub75claim_on_pio  -> adapt!!!
  configure_pio     significant changes
                    span all relevant GPIOS:
                        int gpio_pins[] = {
        DATA_BASE_PIN, DATA_BASE_PIN + 5,                     // 6 RGB pins
        ROWSEL_BASE_PIN, ROWSEL_BASE_PIN + ROWSEL_N_PINS - 1, // row-select pins
        CLK_PIN,
        STROBE_PIN,
        OEN_PIN};
    int n = sizeof(gpio_pins) / sizeof(gpio_pins[0]);


setup_bitplane_creation ✅

read_chan_handler renamed to handle_bitplane_irq ✅
ctrl_chan_handler renamed to handle_ctrl_irq ✅


ns_to_pio_cycles ✅
build_row_cmd_buffer ✅
encode_row_address ✅
compute_bcm_cycles ✅
cie1931_inverse ✅
void setIntensity(float intensity, bool linear_brightness_control) ✅
void setIntensity(float intensity, bool linear_brightness_control = true) ✅
setBasisBrightness ✅
start_hub75_driver renamed to start ✅
create_hub75_driver renamed to create ✅

hub75_timing_init renamed to timing_init and modified - no hub75_timing_recompute

Things to check in detail:
setup_display_irq  ???
setup_bitplane_stream_irq ???
handle_bitplane_irq ???
timing_init ???

CMakeLists.txt - todo: analyse!!!

hub75_demo_dual.cpp - demo programm to drive to different panel types

hub75.cpp
member functions
register and unregister driver instances
management of IRQs for each instance

hub75.hpp
class definition


