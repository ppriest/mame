// license:BSD-3-Clause
// copyright-holders: NaokiS

#include "emu.h"

#include "pl6_pic.h"

#include "multibyte.h"

#define LOG_PIC     (1U << 1)
#define VERBOSE     (0)

#include "logmacro.h"

#define LOGPIC(...)     LOGMASKED(LOG_PIC,     __VA_ARGS__)

pl6pic_device::pl6pic_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, HEBER_PLUTO6_PIC, tag, owner, clock),
	device_nvram_interface(mconfig, *this),
	i2c_hle_interface(mconfig, *this, 0x60),
	m_timer(nullptr),
	m_hb_led(*this, "heartbeat_led"),
	m_dip(*this, "SW%u", 1U),
	m_stake(*this, "Stake-Jackpot"),
	m_perc(*this, "Percentage"),
	m_secsw(*this, "SECSW")
{
}

void pl6pic_device::device_start()
{
	// Heartbeat LED flash
	m_timer = timer_alloc(FUNC(pl6pic_device::heartbeat_callback), this);
	m_timer->adjust(attotime::from_seconds(1), 0, attotime::from_seconds(1));

	system_time systime;
	machine().base_datetime(systime);
	s64 days = systime.local_time.day;
	for (int year = 2000; year < systime.local_time.year; year++)
		days += ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0)) ? 366 : 365;
	m_rtc_base = ((days * 24 + systime.local_time.hour) * 60 + systime.local_time.minute) * 60 + systime.local_time.second;

	save_item(NAME(m_heartbeat));
	save_item(NAME(m_reg));
	save_item(NAME(m_write_count));
	save_item(NAME(m_read_count));
	save_item(NAME(m_latched));
	save_item(NAME(m_rtc_adjust));
	save_item(NAME(m_log));
	save_item(NAME(m_customer_data));
}

TIMER_CALLBACK_MEMBER(pl6pic_device::heartbeat_callback)
{
	m_heartbeat ^= 1;
	m_hb_led = m_heartbeat;
}

void pl6pic_device::nvram_default()
{
	m_rtc_adjust = 0;
	memset(m_log, 0, sizeof(m_log));
	memset(m_customer_data, 0xff, sizeof(m_customer_data));
}

bool pl6pic_device::nvram_read(util::read_stream &file)
{
	u8 buf[4 + sizeof(m_log) + sizeof(m_customer_data)];
	auto const [err, actual] = util::read(file, buf, sizeof(buf));
	if (err || actual != sizeof(buf))
		return false;
	m_rtc_adjust = get_u32le(buf);
	memcpy(m_log, buf + 4, sizeof(m_log));
	memcpy(m_customer_data, buf + 4 + sizeof(m_log), sizeof(m_customer_data));
	return true;
}

bool pl6pic_device::nvram_write(util::write_stream &file)
{
	u8 buf[4 + sizeof(m_log) + sizeof(m_customer_data)];
	put_u32le(buf, m_rtc_adjust);
	memcpy(buf + 4, m_log, sizeof(m_log));
	memcpy(buf + 4 + sizeof(m_log), m_customer_data, sizeof(m_customer_data));
	auto const [err, actual] = util::write(file, buf, sizeof(buf));
	return !err;
}

u32 pl6pic_device::rtc_seconds() const
{
	return u32(m_rtc_base + m_rtc_adjust + machine().time().seconds());
}

INPUT_CHANGED_MEMBER(pl6pic_device::security_switch_changed)
{
	// new entries go first, the oldest is dropped
	memmove(m_log[1], m_log[0], sizeof(m_log[0]) * (LOG_ENTRIES - 1));
	put_u32le(m_log[0], rtc_seconds());
	m_log[0][4] = m_secsw->read() & 0x0f;
}

void pl6pic_device::i2c_start()
{
	m_write_count = 0;
	m_read_count = 0;
}

void pl6pic_device::i2c_stop()
{
	// a sub-address with no data
	if (m_write_count == 1 && m_reg == 0x0b)
	{
		LOGPIC("clear switch log\n");
		memset(m_log, 0, sizeof(m_log));
	}
}

void pl6pic_device::write_data(u16 offset, u8 data)
{
	const unsigned index = m_write_count++;
	if (index == 0)
	{
		m_reg = data;
		return;
	}

	LOGPIC("write reg %02x byte %u = %02x\n", m_reg, index - 1, data);
	switch (m_reg)
	{
	case 0x00: // set RTC, LSB first
		if (index <= 4)
		{
			m_latched[index - 1] = data;
			if (index == 4)
				m_rtc_adjust += s32(get_u32le(m_latched) - rtc_seconds());
		}
		break;

	case 0x14: // customer data: count, then data
		if (index >= 2 && index - 2 < std::size(m_customer_data))
			m_customer_data[index - 2] = data;
		break;

	default:
		logerror("write to unknown register %02x = %02x\n", m_reg, data);
		break;
	}
}

u8 pl6pic_device::read_data(u16 offset)
{
	const u8 data = reg_byte(m_reg, m_read_count++);
	LOGPIC("read reg %02x byte %u = %02x\n", m_reg, m_read_count - 1, data);
	return data;
}

u8 pl6pic_device::reg_byte(u8 reg, unsigned index)
{
	switch (reg)
	{
	case 0x00: // RTC
		if (index == 0)
			put_u32le(m_latched, rtc_seconds());
		return (index < 4) ? m_latched[index] : 0xff;

	case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
	case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: // switch log
		return (index < 5) ? m_log[reg - 1][index] : 0xff;

	case 0x0c: return m_perc->read();
	case 0x0d: return m_stake->read();
	case 0x0e: return ~m_dip[0]->read() & 0xff;
	case 0x0f: return ~m_dip[1]->read() & 0xff;

	case 0x10: // status
	case 0x11: // revision
	case 0x12: // signature
		return 0x00;

	case 0x13: // RTC, preceded by 1/128 second count
		if (index == 0)
		{
			const attotime now = machine().time();
			m_latched[0] = u8(now.attoseconds() / (ATTOSECONDS_PER_SECOND / 128));
			put_u32le(&m_latched[1], rtc_seconds());
		}
		return (index < 5) ? m_latched[index] : 0xff;

	case 0x15: // customer data
		return (index < std::size(m_customer_data)) ? m_customer_data[index] : 0xff;

	default:
		logerror("read from unknown register %02x\n", reg);
		return 0xff;
	}
}

INPUT_PORTS_START(pluto6_csd_pic)
	PORT_START( "SW1" )
	PORT_DIPNAME( 0x00000001, 0x00000001, "SW1: 1" ) PORT_DIPLOCATION( "SW1:1" )
	PORT_DIPSETTING(          0x00000001, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000002, 0x00000002, "SW1: 2" ) PORT_DIPLOCATION( "SW1:2" )
	PORT_DIPSETTING(          0x00000002, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000004, 0x00000004, "SW1: 3" ) PORT_DIPLOCATION( "SW1:3" )
	PORT_DIPSETTING(          0x00000004, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000008, 0x00000008, "SW1: 4" ) PORT_DIPLOCATION( "SW1:4" )
	PORT_DIPSETTING(          0x00000008, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000010, 0x00000010, "SW1: 5" ) PORT_DIPLOCATION( "SW1:5" )
	PORT_DIPSETTING(          0x00000010, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000020, 0x00000020, "SW1: 6" ) PORT_DIPLOCATION( "SW1:6" )
	PORT_DIPSETTING(          0x00000020, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000040, 0x00000040, "SW1: 7" ) PORT_DIPLOCATION( "SW1:7" )
	PORT_DIPSETTING(          0x00000040, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000080, 0x00000080, "SW1: 8" ) PORT_DIPLOCATION( "SW1:8" )
	PORT_DIPSETTING(          0x00000080, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )

	PORT_START( "SW2" )
	PORT_DIPNAME( 0x00000001, 0x00000001, "SW2: 1" ) PORT_DIPLOCATION( "SW2:1" )
	PORT_DIPSETTING(          0x00000001, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000002, 0x00000002, "SW2: 2" ) PORT_DIPLOCATION( "SW2:2" )
	PORT_DIPSETTING(          0x00000002, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000004, 0x00000004, "SW2: 3" ) PORT_DIPLOCATION( "SW2:3" )
	PORT_DIPSETTING(          0x00000004, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000008, 0x00000008, "SW2: 4" ) PORT_DIPLOCATION( "SW2:4" )
	PORT_DIPSETTING(          0x00000008, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000010, 0x00000010, "SW2: 5" ) PORT_DIPLOCATION( "SW2:5" )
	PORT_DIPSETTING(          0x00000010, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000020, 0x00000020, "SW2: 6" ) PORT_DIPLOCATION( "SW2:6" )
	PORT_DIPSETTING(          0x00000020, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000040, 0x00000040, "SW2: 7" ) PORT_DIPLOCATION( "SW2:7" )
	PORT_DIPSETTING(          0x00000040, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )
	PORT_DIPNAME( 0x00000080, 0x00000080, "SW2: 8" ) PORT_DIPLOCATION( "SW2:8" )
	PORT_DIPSETTING(          0x00000080, DEF_STR( Off ) )
	PORT_DIPSETTING(          0x00000000, DEF_STR( On ) )

	PORT_START("Stake-Jackpot")
	PORT_DIPNAME( 0x70, 0x60, "Stake" )        PORT_DIPLOCATION("Stake-Jackpot:5,6,7")
	PORT_DIPSETTING(    0x00, "5p" )
	PORT_DIPSETTING(    0x10, "10p" )
	PORT_DIPSETTING(    0x20, "20p" )
	PORT_DIPSETTING(    0x30, "25p" )
	PORT_DIPSETTING(    0x40, "30p" )
	PORT_DIPSETTING(    0x50, "40p" )
	PORT_DIPSETTING(    0x60, "50p" )
	PORT_DIPSETTING(    0x70, "£1" )
	PORT_DIPNAME( 0x0f, 0x03, "Jackpot" )        PORT_DIPLOCATION("Stake-Jackpot:1,2,3,4")
	PORT_DIPSETTING(    0x00, "No key" )
	PORT_DIPSETTING(    0x01, "£3 Cash" )
	PORT_DIPSETTING(    0x02, "£4 Cash" )
	PORT_DIPSETTING(    0x03, "£5 Cash" )
	PORT_DIPSETTING(    0x04, "£6 Cash" )
	PORT_DIPSETTING(    0x05, "£6 Token" )
	PORT_DIPSETTING(    0x06, "£8 Cash" )
	PORT_DIPSETTING(    0x07, "£8 Cash" )
	PORT_DIPSETTING(    0x08, "£10 Cash" )
	PORT_DIPSETTING(    0x09, "£15 Cash" )
	PORT_DIPSETTING(    0x0a, "£25 Cash" )
	PORT_DIPSETTING(    0x0b, "£25 LBO" )
	PORT_DIPSETTING(    0x0c, "£35 Cash" )
	PORT_DIPSETTING(    0x0d, "£70 Cash" )
	PORT_DIPSETTING(    0x0e, "£100 Cash" )
	PORT_DIPSETTING(    0x0f, "Invalid" )
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("Percentage")
	PORT_DIPNAME( 0x0f, 0x05, "Percentage" )        PORT_DIPLOCATION("Percentage:1,2,3,4")
	PORT_DIPSETTING(    0x00, "No key" )
	PORT_DIPSETTING(    0x01, "70%" )
	PORT_DIPSETTING(    0x02, "72%" )
	PORT_DIPSETTING(    0x03, "74%" )
	PORT_DIPSETTING(    0x04, "76%" )
	PORT_DIPSETTING(    0x05, "78%" )
	PORT_DIPSETTING(    0x06, "80%" )
	PORT_DIPSETTING(    0x07, "82%" )
	PORT_DIPSETTING(    0x08, "84%" )
	PORT_DIPSETTING(    0x09, "86%" )
	PORT_DIPSETTING(    0x0a, "88%" )
	PORT_DIPSETTING(    0x0b, "90%" )
	PORT_DIPSETTING(    0x0c, "92%" )
	PORT_DIPSETTING(    0x0d, "94%" )
	PORT_DIPSETTING(    0x0e, "96%" )
	PORT_DIPSETTING(    0x0f, "98%" )
	PORT_BIT( 0xf0, IP_ACTIVE_HIGH, IPT_UNUSED )

	PORT_START("SECSW")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Security A") PORT_TOGGLE PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(pl6pic_device::security_switch_changed), 0)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Security B") PORT_TOGGLE PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(pl6pic_device::security_switch_changed), 0)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Security C") PORT_TOGGLE PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(pl6pic_device::security_switch_changed), 0)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_OTHER) PORT_NAME("Security D") PORT_TOGGLE PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(pl6pic_device::security_switch_changed), 0)
INPUT_PORTS_END

ioport_constructor pl6pic_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(pluto6_csd_pic);
}

DEFINE_DEVICE_TYPE(HEBER_PLUTO6_PIC, pl6pic_device, "pl6_pic", "Heber Pluto 6 Customer Security Device")
