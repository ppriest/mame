// license:BSD-3-Clause
// copyright-holders: NaokiS

/*
Heber Pluto 6 PIC18 Customer Security Device

The CSD is used as part of the security, anti-tampering and system controller functions within the Pluto 6.
When the pluto 6 is powered down, a Varta 3.3V Ni-MH battery keeps the PIC18 running in a lower power state.
Is this state, the CSD does the following:
    * Keeps the RTC running.
    * Reads 4 inputs (called power down inputs) and logs the state of them if they change with a timestamp.
    * Retains any current logs.

When the Pluto 6 is powered up, it does the following:
    * Go into a fully powered state
    * Load the encrypted FPGA bitstream using the CPLD to decrypt it.
    * Release reset on the CPU.
    * Read and supply the following over I2C:
        * Stake and Percentage dongles
        * 2x 8-Way DIP switches
        * PIC18 Revision
        * PIC18 Signature

The CPU sees it as an I2C device with byte registers, selected by a sub-address written before each
access (register list from Heber's pic18.h, formats from the pic18.o driver in the Pluto 6 hardware library):
    0x00        RTC, seconds since 2000-01-01 00:00:00, 4 bytes LSB first (written to set the time)
    0x01-0x0a   power down switch log entries, 4 byte timestamp + switch states, timestamp 0 = empty
    0x0b        clear switch log (sub-address write only)
    0x0c        percentage key
    0x0d        stake (bits 6-4) and jackpot (bits 3-0) key
    0x0e, 0x0f  DIL switches SW1, SW2 (1 = on)
    0x10        status
    0x11        CSD revision
    0x12        signature, 4 bytes
    0x13        RTC with 1/128 s count, 5 bytes
    0x14        customer data write: byte count, then up to 8 bytes
    0x15        customer data read

    TODO:
        * "upload" FPGA bitstream
        * Hold CPU in reset?
        * status, revision and signature values are unknown
        * order of the switch log entries is unverified
*/

#ifndef MAME_JPM_PL6_PIC_H
#define MAME_JPM_PL6_PIC_H

#pragma once

#include "machine/i2chle.h"

INPUT_PORTS_EXTERN(pluto6_csd_pic);

class pl6pic_device :
	public device_t,
	public device_nvram_interface,
	public i2c_hle_interface
{
public:
	pl6pic_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	auto sda_rx_cb(){ return write_sda.bind(); }

	DECLARE_INPUT_CHANGED_MEMBER(security_switch_changed);

protected:
	virtual void device_start() override ATTR_COLD;

	virtual ioport_constructor device_input_ports() const override ATTR_COLD;

	// device_nvram_interface
	virtual void nvram_default() override;
	virtual bool nvram_read(util::read_stream &file) override;
	virtual bool nvram_write(util::write_stream &file) override;

	// i2c_hle_interface
	virtual u8 read_data(u16 offset) override;
	virtual void write_data(u16 offset, u8 data) override;
	virtual bool has_subaddress() const override { return false; }
	virtual void i2c_start() override;
	virtual void i2c_stop() override;
	virtual const char *get_tag() override { return tag(); }

private:
	static constexpr unsigned LOG_ENTRIES = 10;

	TIMER_CALLBACK_MEMBER(heartbeat_callback);

	u32 rtc_seconds() const;
	u8 reg_byte(u8 reg, unsigned index);

	emu_timer *m_timer;

	uint8_t m_heartbeat = 1;
	output_finder<> m_hb_led;

	required_ioport_array<2> m_dip;
	required_ioport m_stake;
	required_ioport m_perc;
	required_ioport m_secsw;

	s64 m_rtc_base = 0;         // host clock in seconds since 2000 when emulation started
	u8 m_reg = 0;               // register selected by the last sub-address
	u8 m_write_count = 0;       // bytes written since the last START, including the sub-address
	u8 m_read_count = 0;        // bytes read since the last START
	u8 m_latched[5]{};          // multi-byte register being read, captured on its first byte

	// battery backed
	s32 m_rtc_adjust = 0;       // set time minus host clock
	u8 m_log[LOG_ENTRIES][5]{};
	u8 m_customer_data[8]{};
};

DECLARE_DEVICE_TYPE(HEBER_PLUTO6_PIC, pl6pic_device)

#endif // MAME_JPM_PL6_PIC_H
