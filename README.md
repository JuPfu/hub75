# HUB75 DMA based Driver

Work in progress ...


**Documentation and References**

This project is based on Raspberry Pi's [pico-examples/pio/hub75](https://github.com/raspberrypi/pico-examples/tree/master/pio/hub75) and Pimoroni's [pimoroni/.../drivers/hub75](https://github.com/pimoroni/pimoroni-pico/tree/main/drivers/hub75).

If you want to understand how RGB matrix panels work, the article [Everything You Didn't Want to Know About RGB Matrix Panels](https://news.sparkfun.com/2650) is a good starting point. Binary Coded Modulation (BCM) is described in detail in [LED dimming using Binary Code Modulation](https://www.batsocks.co.uk/readme/art_bcm_3.htm).

***Achievements of the Revised Driver***

The modifications to the [pimeroni hub75 driver](https://github.com/pimoroni/pimoroni-pico/tree/main/drivers/hub75) result in the following improvements:

- work moved from cpu processor to DMA and PIO co-processors
- improved performance by mutually self paced interlinked DMA and PIO processes
- no need for <code>hub75_wait_tx_stall</code> any more and therefore this method can be removed
- reduced code in interrupt handler

In summary, it can be said that this leads to an increase in the performance of the Hub75 driver and, as a side-effect, no ghost images occurred in tests up to 250 MHz system clock.

***Motivation***

For a private project I needed to acquire some knowledge of the Raspberry Pi Pico Microcontroller. On the internet I found the free [Raspberry Pi Pico Lectures 2022](https://www.youtube.com/playlist?list=PLDqMkB5cbBA5oDg8VXM110GKc-CmvUqEZ) by [Hunter Adams](https://vanhunteradams.com/) which are mind blowing good! If your focus is on Picos <b>P</b>rogrammable <b>I</b>nput <b>O</b>utput (PIO) you can jump in at [Raspberry Pi Pico Lecture 15: PIO Overview and Examples](https://www.youtube.com/watch?v=wet9CYpKZOQ).

Hunter Adams remarks about DMA and PIO co-processors inspired me to optimise the HUB75 driver as a self assigned task.

***Genealogy***

****Raspberry Pi Pico HUB75 Example****

The [Pico HUB75 example](https://github.com/raspberrypi/pico-examples/tree/master/pio/hub75) shows how to connect a HUB75 LED matrix panel with Picos PIO capabilities. It is for educational purposes. The emphasis is on clarity and ease of understanding.

The colour palette is generated by modulating the output enable (OE) signal. This is done by applying the Binary Coded Modulation (BCM) algorithm to the OE signal. The modulation is done row by row. For each row, all color bits are modulated before the same process is applied to the next row. <code>hub75_wait_tx_stall</code> is essential for synchronisation. No DMA is used in this implementation to drive performance. 

****Pimoroni HUB75 Driver****

The [pimeroni hub75 driver](https://github.com/pimoroni/pimoroni-pico/tree/main/drivers/hub75) builds on the Raspberry Pi Pico implementation. It improves performance by changing from row-wise to plane-wise handling of modulation and by using DMA for passing pixel data to the PIO state machine.
Pimoroni's implementation relies on <code>hub75_wait_tx_stall</code>, too.

*****Schematics of Pimoronis DMA handling*****

Pixel data transferred to the sm_data state machine is some kind of input - output process, therefore prone to be handled by Direct Memory Access (DMA). DMA-Input are the pixels transferred to the state machines RX-FIFO (DMA-Output).

![hub_pimoroni](assets/pimoroni_dma.png)

***Obstacle <code>hub75_wait_tx_stall</code>***

In Raspberry Pi's and Pimoroni's implementation <code>hub75_wait_tx_stall</code> was indispensable. This method actively waits until the state machine has stalled on empty TX FIFO during a blocking PULL, or an OUT with autopull enabled. Without this method to check if an OEn pulse has finished, things WOULD get out of sequence. This method kind of gets in the way and precludes an approach based on chained DMA channels that then feed PIOs.

```c
static inline void hub75_wait_tx_stall(PIO pio, uint sm) {
    uint32_t txstall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio->fdebug = txstall_mask;
    while (!(pio->fdebug & txstall_mask))
        tight_loop_contents();
}
```
*Code of hub75_wait_tx_stall*

So, is there another way to get the information that the OEn pulse is finished ?
Currently the PIO program <code>hub75_row</code> listed below emits the OEn pulse and is checked by <code>hub75_wait_tx_stall</code>. The program takes a 32 bit word as input. It uses the first 5 bits of the input to address the row. The remaining 27 bits assert Output Enable for the number of cycles specified. In the pulse_loop the OE signal is held low for this number of cycles.

<img src="assets/hub75_row.png" width="360" height="186">

The first step is to add a statement after the pulse loop which emits a value to the RX FIFO. The second step is to setup a DMA channel which retrieves this dummy value from the RX-FIFO. The third step is to establish an interrupt handler for this DMA channel. When this interrupt ist executed it is granted that the pulse loop has finished.

***Schematics of DMA Chains and related PIO State Machines for the Present HUB75 Driver Implementation***

![hub75_dma](assets/hub75_dma.png)
*Picture 1: Chained DMA Channels and assigned PIOs*

The complete sequence and interaction between each DMA and its assigned state-machine is depicted in the above diagram. 

1. As in the pimeroni solution an interlaced double row of pixel data (input) is sent to the LED panels shift register via the ´pixel data´ DMA which feeds the hub75_rdata_gb888 state machine (output). 

2. The ´pixel data´ DMA channel is chained to the ´dummy pixel data´ DMA channel. It seamlessly passes control to this channel when the number of transfers to perform has been reached.

3. At the end of the just emitted row some addtional dummy pixels (input) are sent via the ´dummy pixel data´ DMA channel to the hub75_rgb_data state machine (output). These dummy pixels at the end are required to clock out the last piece of genuine data.
(Easy to see the effect of no dummy data being emitted. Chain the ´pixel data´ DMA channel directly to the ´OEn data´ DMA channel and look for yourself.)

4. The ´dummy pixel data´ DMA channel is chained to the ´OEn data´ DMA channel. It passes control to the ´OEn data´ channel when the number of transfers to perform has been reached.

5. The ´OEn data´ DMA channel takes 32 bits as input and deliveres this data to the hub75_row state machine. Output is enabled for the specified time for each LED in the row addressed if a bit is set in the current bit-plane. In other words, all LEDs in the row are lit which have a colour bit set for the current bit-plane.

6. The 'OEn finished' DMA channel is patiently waiting for the output of the ´OEn data´ DMA channel. The value of the data (4,294,967,295) is irrelevant. Relevant is only when the data is delivered. This is the case after the OEn pulse has finished. The 'OEn finished' DMA channel has an attached interrupt handler ´oen_finished_handler´. This handler sets up the DMA channels for the next row and for the OEn pulse.









