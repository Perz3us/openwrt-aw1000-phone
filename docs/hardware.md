# Hardware notes

Stuff I found while getting the phone port working. Tested on NoobWRT 25.12.5, kernel 6.12.94.

## Chips and pins

SoC is the IPQ8074. The SLIC is a Skyworks Si32261 (Si3226x, chip ID 0xC3). The stock device
tree calls it si3217x, ignore that. It has two channels but only channel 1 goes to the RJ11
jack. The modem is a Quectel RG500Q-EA.

```
gpio33  PCM DRX (into IPQ)      gpio46  SLIC reset
gpio34  PCM DTX (out of IPQ)    gpio47  SLIC interrupt
gpio35  PCM FSYNC               gpio50  SPI clock
gpio36  PCM PCLK                gpio51  SPI chip select
                                gpio52  SPI MISO
                                gpio53  SPI MOSI
```

SPI is bit-banged (mode 3), same as the stock driver.

## Clocks

The SLIC reads back 0xFF on SPI until it gets a PCM clock, and OpenWrt never turns the audio
clocks on. That's why this needs a kernel module.

1. Enable gcc_adss_pwm_clk (GCC + 0x1c020). Before that, touching anything in ADSS hangs the SoC.
2. Start the audio PLL (GCC + 0x4b000) by hand, it isn't in vote mode on this board.
   L = 0x28, alpha = 0xF5C28F5C29, post div 4.
3. PCM clock = PLL / 12 / 2 = 8.192 MHz, 16 slots x 16 bit.

When the clocks are right, SLIC register 3 (MSTRSTAT) reads 0x1F.

## Don't do this

- Reading ADSS_GLB + 0x04: external abort, router dies.
- Writing ADSS_GLB + 0x00 (PCM reset in QSDK): bus hang, watchdog reboot a few seconds later,
  nothing in the log.

Get a serial console (115200) before poking at ADSS. The abort messages only show up there.

## PCM

Modem and SLIC are both slaves. Both send on the IPQ RX line and listen on the IPQ TX line,
so they can't hear each other. The driver copies samples between them.

CPU mode is PCM_CTRL bit 29. FIFO_CNT has TX count in bits 8:0 and RX count in 17:9.
RX16 (0x30) and TX16 (0x34) move one sample. Each FIFO word has the slot number in bits 20:16.
TX words need it too, otherwise they only go out in slot 0. Stopping the block doesn't clear
the FIFOs.

Timing: both chips send one PCLK after the slot boundary. Modem "slot 1" sends on frame bits
1-16 and listens on 0-15. The SLIC listens one PCLK earlier than it sends, so with TX count
31 and RX count 32 it uses bits 32-47 both ways.

So: read modem from slot 1, write modem to slot 0, phone is slot 32 both ways. The bridge
thread runs every ~1 ms with 24 frames queued in TX.

## SLIC settings

Stock driver is ProSLIC API 9.2.0, LCUB setup. I kept the stock general config.

- DC feed 48 V / 20 mA. Measured 45.8 V on hook, 20 mA and 8.5 V off hook.
- Ringing 20 Hz, 45 Vrms, 2 s on / 4 s off. (Telstra uses 25 Hz, ~70 Vrms.)
- 600 ohm impedance.
- Dial tone 425 Hz, busy 425 Hz 0.5/0.5 s.
- DTMF digit: IRQ2 bit 4, digit in low nibble of TONDTMF.
- The interrupt is level low until the IRQ registers are read, so the kernel masks it until
  fxsd acks.

## Patch file

The SLIC needs Skyworks' DSP patch after every reset. Stock firmware uses patch ID 0x05262017
(si3226x_patch_C_FB_2017MAY26). tools/mkpatch.py turns the C source into this format
(little endian):

```
"FXSP"
u32  patch ID
u32  n
u32  data[n]          written to PRAM from address 0
u16  entries[16]
u32  m
u16  ram_addr[m]
u32  ram_data[m]
```

## Modem

- AT+QDAI sets the PCM mode. Factory is x,0,0,4,0,0,1,1 (master). It needs x,1,0,4,0,0,1,1
  (slave), then AT+CFUN=1,1. Yes the first field is a literal x.
- ttyUSB2 is held by QModem's at-daemon, so everything goes through ubus.
- No RING or +CLIP on any port, so fxsd polls AT+CLCC.
- Use AT+CHUP to hang up. ATH kills data calls too.
- Levels: AT+CLVL=5, AT+QRXGAIN=8192, SLIC TX gain -6 dB (otherwise it clips).

## Links

- https://forum.openwrt.org/t/cli-phone-port-rj11-support-for-voice-calls-on-arcadyan-aw1000-router/216780
- https://github.com/kylemd/Arcadyan-AW1000-IPQ807x-Firmware-Dump
- https://lkml.iu.edu/hypermail/linux/kernel/1302.3/01753.html (2013 Si3226x driver)
