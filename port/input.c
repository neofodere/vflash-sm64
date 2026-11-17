#include <ultra64.h>
#include "vf_hw.h"

#define UART_RBR VF_UART0
#define UART_IER (VF_UART0 + 0x04)
#define UART_LSR (VF_UART0 + 0x14)

#define BTN_ENTER (1u << 2)

#define COLOUR_RED 0
#define COLOUR_YELLOW 1
#define COLOUR_GREEN 2
#define COLOUR_BLUE 3

#define COLOUR_BIT_MIN 10
#define COLOUR_BIT_MAX 14
#define NO_COLOUR 0xFF

#define PULSE_FRAMES 3
#define LEARN_WINDOW 4

static u8 sPacket[48];
static u32 sPacketLen;
static u32 sPacketNeed;
static u32 sButtons;
static s32 sStickX;
static s32 sStickY;
static u8 sEventTimer[4];
static u8 sHaveFullReport;
static u8 sOpened;
static u32 sColours;
static u8 sBitColour[COLOUR_BIT_MAX + 1] = {
	[10] = NO_COLOUR, [11] = COLOUR_YELLOW, [12] = COLOUR_RED, [13] = COLOUR_BLUE, [14] = COLOUR_GREEN,
};
static u8 sLearnEvent[4];
static u8 sLearnBits[COLOUR_BIT_MAX + 1];

static void send_packet(const u8 *payload, u32 n)
{
	u8 body[8];
	u32 sum = 1 + n;
	u32 i, m;

	for (i = 0; i < n; i++) {
		body[i] = payload[i];
		sum += payload[i];
	}
	body[n] = (u8) sum;
	body[n + 1] = (u8) (sum >> 8);
	m = n + 2;
	vf_putc((char) 0xFF);
	vf_putc(0x01);
	vf_putc((char) n);
	for (i = 0; i < m; i += 2) {
		u32 b0 = body[i];
		u32 b1 = i + 1 < m ? body[i + 1] : 0;

		vf_putc((char) (0x40 | (b0 & 15)));
		vf_putc((char) (0x40 | (b0 >> 4) | (b1 & 3) << 4));
		vf_putc((char) (0x40 | (b1 >> 2)));
	}
}

static s32 sign_extend8(u32 v)
{
	return (s32) (s8) v;
}

/*
 * The colour buttons arrive twice: as press events that name the colour, and as
 * bits of the stick report whose positions differ between controller models and
 * emulators. A bit that rises shortly after exactly one colour event belongs to
 * that colour.
 */
static void learn_colours(void)
{
	u32 colour, bit;

	for (colour = 0; colour < 4; colour++) {
		u32 found = NO_COLOUR;

		if (!sLearnEvent[colour])
			continue;
		for (bit = COLOUR_BIT_MIN; bit <= COLOUR_BIT_MAX; bit++) {
			if (!sLearnBits[bit])
				continue;
			if (found != NO_COLOUR) {
				found = NO_COLOUR - 1;
				break;
			}
			found = bit;
		}
		if (found >= NO_COLOUR - 1)
			continue;
		for (bit = COLOUR_BIT_MIN; bit <= COLOUR_BIT_MAX; bit++)
			if (sBitColour[bit] == colour)
				sBitColour[bit] = NO_COLOUR;
		sBitColour[found] = (u8) colour;
		sLearnEvent[colour] = 0;
		sLearnBits[found] = 0;
	}
}

static void age_learning(void)
{
	u32 i;

	for (i = 0; i < 4; i++)
		if (sLearnEvent[i])
			sLearnEvent[i]--;
	for (i = COLOUR_BIT_MIN; i <= COLOUR_BIT_MAX; i++)
		if (sLearnBits[i])
			sLearnBits[i]--;
}

static void decode_packet(void)
{
	u8 raw[40];
	u32 n = sPacket[2];
	u32 groups = (n + 3) / 2;
	u32 i, sum = 0;

	for (i = 0; i < groups; i++) {
		u32 g0 = sPacket[3 + i * 3] & 0x3F;
		u32 g1 = sPacket[4 + i * 3] & 0x3F;
		u32 g2 = sPacket[5 + i * 3] & 0x3F;

		raw[i * 2] = (u8) (g0 | (g1 & 15) << 4);
		raw[i * 2 + 1] = (u8) ((g1 >> 4) | g2 << 2);
	}
	sum = 1 + n;
	for (i = 0; i < n; i++)
		sum += raw[i];
	if ((u8) sum != raw[n] || (u8) (sum >> 8) != raw[n + 1])
		return;
	if (n == 6) {
		u32 x = (raw[0] >> 4) | (raw[1] & 15) << 4;
		u32 y = (raw[1] >> 5) | (raw[2] & 31) << 3;

		sStickY = sign_extend8(x);
		sStickX = sign_extend8(y);
		u32 buttons = (raw[2] >> 6) | raw[3] << 2 | (u32) (raw[4] & 63) << 10;
		u32 rising = buttons & ~sButtons;
		u32 bit;

		for (bit = COLOUR_BIT_MIN; bit <= COLOUR_BIT_MAX; bit++)
			if (rising & 1u << bit)
				sLearnBits[bit] = LEARN_WINDOW;
		sButtons = buttons;
		sHaveFullReport = 1;
		learn_colours();
	} else if (n == 4 && !sHaveFullReport) {
		s32 y = raw[0] | (raw[1] & 15) << 8;
		s32 x = (raw[1] >> 4) | raw[2] << 4;

		sStickX = (x - 0x800) >> 4;
		sStickY = (y - 0x800) >> 4;
		sButtons = (sButtons & ~BTN_ENTER) | (raw[3] & 1 ? BTN_ENTER : 0);
	} else if (n == 2 && raw[0] >= 1 && raw[0] <= 4) {
		if (!sHaveFullReport)
			sEventTimer[raw[0] - 1] = PULSE_FRAMES;
		sLearnEvent[raw[0] - 1] = LEARN_WINDOW;
		learn_colours();
	}
}

static void feed(u32 b)
{
	if (b == 0xFF) {
		sPacketLen = 0;
		sPacketNeed = 0;
	}
	if (sPacketLen == 0 && b != 0xFF)
		return;
	if (sPacketLen < sizeof(sPacket))
		sPacket[sPacketLen++] = (u8) b;
	if (sPacketLen == 3)
		sPacketNeed = 3 + (sPacket[2] + 3) / 2 * 3;
	if (sPacketNeed && sPacketLen == sPacketNeed) {
		decode_packet();
		sPacketLen = 0;
		sPacketNeed = 0;
	}
}

void vf_input_poll(void)
{
	static const u8 query[2] = { 0x40, 0x03 };
	u32 i;

	if (!sOpened) {
		VF_REG(UART_IER) |= 1;
		sOpened = 1;
	}
	send_packet(query, 2);
	while (VF_REG(UART_LSR) & 1)
		feed(VF_REG(UART_RBR) & 0xFF);
	age_learning();
	sColours = 0;
	if (sHaveFullReport) {
		u32 bit;

		for (bit = COLOUR_BIT_MIN; bit <= COLOUR_BIT_MAX; bit++)
			if (sButtons & 1u << bit && sBitColour[bit] != NO_COLOUR)
				sColours |= 1u << sBitColour[bit];
	} else {
		sButtons &= BTN_ENTER;
		for (i = 0; i < 4; i++) {
			if (sEventTimer[i]) {
				sEventTimer[i]--;
				sColours |= 1u << i;
			}
		}
	}
}

static s8 scale_stick(s32 v)
{
	v = v * 80 / 127;
	return (s8) (v > 80 ? 80 : v < -80 ? -80 : v);
}

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *status)
{
	u32 i;

	for (i = 0; i < 4; i++) {
		status[i].type = i == 0 ? CONT_TYPE_NORMAL : 0;
		status[i].status = 0;
		status[i].errnum = i == 0 ? 0 : CONT_NO_RESPONSE_ERROR;
	}
	*bitpattern = 1;
	return 0;
}

s32 osContStartReadData(OSMesgQueue *mq)
{
	vf_input_poll();
	osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
	return 0;
}

void osContGetReadData(OSContPad *pad)
{
	u32 buttons = 0;
	s32 x = sStickX;
	s32 y = sStickY;
	u32 i;

	if (sColours & 1u << COLOUR_GREEN)
		buttons |= A_BUTTON;
	if (sColours & 1u << COLOUR_RED)
		buttons |= B_BUTTON;
	if (sColours & 1u << COLOUR_YELLOW)
		buttons |= Z_TRIG;
	if (sButtons & BTN_ENTER)
		buttons |= START_BUTTON;
	if (sColours & 1u << COLOUR_BLUE) {
		if (x < -40)
			buttons |= L_CBUTTONS;
		if (x > 40)
			buttons |= R_CBUTTONS;
		if (y > 40)
			buttons |= U_CBUTTONS;
		if (y < -40)
			buttons |= D_CBUTTONS;
		x = 0;
		y = 0;
	}
	for (i = 0; i < 4; i++) {
		pad[i].button = 0;
		pad[i].stick_x = 0;
		pad[i].stick_y = 0;
		pad[i].errnum = i == 0 ? 0 : CONT_NO_RESPONSE_ERROR;
	}
	pad[0].button = (u16) buttons;
	pad[0].stick_x = scale_stick(x);
	pad[0].stick_y = scale_stick(y);
}

s32 osEepromProbe(OSMesgQueue *mq)
{
	return EEPROM_TYPE_4K;
}

static u8 sEeprom[512];

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int length)
{
	u32 i;

	for (i = 0; i < (u32) length; i++)
		buffer[i] = sEeprom[(address * 8 + i) & 511];
	return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int length)
{
	u32 i;

	for (i = 0; i < (u32) length; i++)
		sEeprom[(address * 8 + i) & 511] = buffer[i];
	return 0;
}
