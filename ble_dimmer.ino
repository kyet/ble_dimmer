/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <kyet@me.com> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.         Isaac K. Ko
 * ----------------------------------------------------------------------------
 */

/* References
 * http://playground.arduino.cc/Main/ACPhaseControl
 * http://playground.arduino.cc/Code/ACPhaseControl
 * http://alfadex.com/2014/02/dimming-230v-ac-with-arduino-2/
 * http://arduinotehniq.blogspot.kr/2014/10/ac-light-dimmer-with-arduino.html
 * https://arduinodiy.wordpress.com/2012/10/19/dimmer-arduino/
 * http://www.instructables.com/id/Arduino-controlled-light-dimmer-The-circuit/
 */

#include <stdarg.h>
#include <TimerOne.h>        // https://github.com/PaulStoffregen/TimerOne
#include <SoftwareSerial.h>  // https://www.arduino.cc/en/Reference/SoftwareSerial
#include <PacketSerial.h>    // https://github.com/bakercp/PacketSerial

/* device type */
#define SWITCH       0x01
#define DIMMER       0x02
const byte devType = DIMMER;

// undefine for release
#define __DEBUG__

/* data type */
#define TYPE_RAW         0x01
#define TYPE_DIMMING1    0x02
#define TYPE_UNDEF6      0x04
#define TYPE_UNDEF5      0x08
#define TYPE_UNDEF4      0x10
#define TYPE_UNDEF3      0x20
#define TYPE_UNDEF2      0x40
#define TYPE_UNDEF1      0x80

/* for dimmer */
#define ZERO_CROSS_INT   0     // for zero crossing detect (int0 is pin 2)
#define HZ               60    // 60 in the most America, Korea, Taiwan, Phillippines
                               // 50 in China, Europe, Japan, and rest of.

/* for bluetooth */
#define BLE_RX           10    // serial pin: connect reverse order
#define BLE_TX           11
#define BLE_DATA_MAX     20    // payload size

/* outlet pin */
const byte outlet[] = {3, 5};
const byte nOutlet  = sizeof(outlet) / sizeof(outlet[0]);

/* for dimmer */
/* NOTE: calcuation of dimPeriod
 *       sec to us, hertz to frequency, half cycle, range
 *       zero crossing period: 8,333us for 60Hz. 10,000us for 50Hz.
 *       dimPeriod when dimRange is 128: 65 for 60Hz, 78 for 50Hz. 
 *       dimPeriod when dimRange is 258: 32 for 60Hz, 39 for 50Hz. 
 * NOTE: When dim range is 128, 
         dimming values below 10 and above 124 not works in experimental. 
 *       So we use threshold low and high.
 */
const byte dimRange   = 128;                   // greater than 128 may not work
const byte dimThresLo = dimRange >> 3;         // approximately 10% of dimRange
const byte dimThresHi = dimRange - dimThresLo; // approximately 90% of dimRange
const byte dimPeriod  = (byte)((1000.0 * 1000.0) / (HZ * 2 * dimRange));
volatile byte dimming[nOutlet]  = {0}; // the time to idle low on the outlet[]
volatile byte crossing[nOutlet] = {0}; // zero-crossing event
volatile byte trigCnt = 0;

SoftwareSerial bleSerial(BLE_RX, BLE_TX);
PacketSerial   pktSerial;

void setup() 
{
	Serial.begin(9600);

	// wait for serial port to connect
	// needed for native USB port only
	while (!Serial) { ; }

	for (int i=0; i<nOutlet; i++)
	{
		pinMode(outlet[i], OUTPUT);
	}

	bleSerial.begin(9600);

	pktSerial.setPacketHandler(&bleParser);
	pktSerial.begin(&bleSerial);

	if (devType == DIMMER)
	{
		pinMode(ZERO_CROSS_INT, INPUT);
		attachInterrupt(ZERO_CROSS_INT, zeroCrossInt, RISING);  
		Timer1.initialize();
		Timer1.attachInterrupt(triggerTriac, dimPeriod);
	}
}

void zeroCrossInt()
{
	// set event flag
	for (int i=0; i<nOutlet; i++)
	{
		// dimming[] indicate delay time. lower is on, higher is off
		if (dimming[i] < dimThresLo)
		{
			digitalWrite(outlet[i], HIGH);
			crossing[i] = 0;
		}
		else if (dimming[i] > dimThresHi)
		{
			digitalWrite(outlet[i], LOW);
			crossing[i] = 0;
		}
		else
		{
			crossing[i] = 1;
		}
	}

	// reset count
	trigCnt = 0;
}

/* NOTE: Do not use delay() in interrupt. It depends on interrupt itself.
 *       Instead, use delayMicroseconds() that run busy loop(NOP).
 */
void triggerTriac()
{
	for (int i=0; i<nOutlet; i++)
	{
		if (crossing[i] && (trigCnt == dimming[i]))
		{
			digitalWrite(outlet[i], HIGH);
			delayMicroseconds(10);  // propagation delay
			digitalWrite(outlet[i], LOW);

			crossing[i] = 0;        // reset event flag
		}
	}

	trigCnt++;
}

// event handler
typedef struct {
	byte port;
	byte value;
} rawValue;

/* Payload (data) 
 * .----------------------------------------.
 * | rawValue1 | rawVale2 | ... | rawValueN |
 * |-----------+----------+-----+-----------|
 * |         2 |        2 | ... |         2 |
 * '----------------------------------------'
 * N is up to 9 ( (BLE_DATA_MAX-2) / sizeof(rawValue) )
 */
void bleRaw(byte *data, byte sz)
{
	byte rvSize  = sizeof(rawValue);
	rawValue *rv = NULL;

	// sanity check
	if (sz < rvSize)             { return; }
	if ((sz % rvSize) != 0)      { return; }
	if ((sz / rvSize) > nOutlet) { return; }

	dumpPkt(data, sz);

	for (int i=0; i<sz; i+=rvSize)
	{
		rv = (rawValue *)(data + i);

		if (rv->port == 0) { continue; }
		rv->port--;

		if (devType == SWITCH)
		{
			if (rv->value >= 0x80)
			{
				digitalWrite(outlet[rv->port], HIGH);
				syslog("LED %d(%dpin) ON(0x%02X)", 
						rv->port, outlet[rv->port], rv->value);
			}
			else
			{
				digitalWrite(outlet[rv->port], LOW);
				syslog("LED %d(%dpin) OFF(0x%02X)",
						rv->port, outlet[rv->port], rv->value);
			}
		}
		else
		{
			dimming[rv->port] = (dimRange - rv->value);
			syslog("LED %d(%dpin) Value(0x%02X)",
					rv->port, outlet[rv->port], rv->value);
		}
	}
}

// y = ax^2 + bx + c
inline byte bleDimmingLinear(byte x, int8_t a, byte da, int8_t b, byte db, int8_t c)
{
	int y = 0;
	
	// prevent divide by zero
	if (da == 0) { da = 1; }
	if (db == 0) { db = 1; }

	y = a*x*x/da + b*x/db + c;

	if (y > dimRange) { return dimRange; }
	else if (y < 0)   { return 0; }

	return y;
}

/* NOTE: small divider could be OK.
         you can use 4bit per divider and merge two byte divider into one byte.
		 then, we can control three outlet within 20 bytes
 */
typedef struct {
	byte   port;
	byte   duration; // duration * 10 = 1 seconds
	int8_t  a;       // coefficient. signed byte
	uint8_t da;      // divider for coefficient
	int8_t  b;
	uint8_t db;
	int8_t  c;       // last constant doesn't need divider
} dimValue;

/* Payload (data) 
 * .-----------------------------------------.
 * | dimValue1 | dimValue2 | ... | dimValueN |
 * |-----------+-----------+-----+-----------|
 * |         7 |         7 | ... |         7 |
 * '-----------------------------------------'
 * N is up to 2 ( (BLE_DATA_MAX-2) / sizeof(dimValue) )
 * duration is transition time
 * if function is y = -1/128x^2 + 2x, you can set
 * (ca, da) = (-1, 128)
 * (cb, db) = ( 2,   1)
 * (cc    ) = ( 0     )
 *
 * NOTE: this funtion use busy loop
 */
void bleDimming(byte *data, byte sz)
{
	byte dimSize   = sizeof(dimValue);
	byte y         = 0;
	dimValue *dv   = NULL;
	uint16_t delta = 0;

	// sanity check
	if (sz < dimSize)             { return; }
	if ((sz % dimSize) != 0)      { return; }
	if ((sz / dimSize) > nOutlet) { return; }
	if (devType != DIMMER)        { return; }

	dumpPkt(data, sz);

	for (int i=0; i<sz; i+=dimSize)
	{
		dv = (dimValue *)(data + i);

		if (dv->duration > dimRange) { continue; }
		if (dv->port == 0)           { continue; }
		dv->port--;

		syslog("LED %d(%dpin) Duration(%d) y=(%d/%d)x^2 + (%d/%d)x + (%d)",
				dv->port, outlet[dv->port], dv->duration, 
				dv->a, dv->da, dv->b, dv->db, dv->c);

		delta = dv->duration*100 / dimRange;
		for (int i=0; i<dimRange; i++)
		{
			y = bleDimmingLinear(i, dv->a, dv->da, dv->b, dv->db, dv->c);
			dimming[dv->port] = dimRange - y;
			delay(delta);
		}
	}
}

/* Datagram (buffer)
   .----------------------------------.
   | Type | Length | Payload          |
   |------+--------+------------------|
   |    1 |      1 | BLE_DATA_MAX - 2 |
   '----------------------------------'
*/
void bleParser(const byte* buffer, size_t size)
{
	byte datagram[BLE_DATA_MAX] = {0};
	byte type, sz;
	char buf[128];

	if (buffer == NULL)
		return; 

	if (size > BLE_DATA_MAX)
		return; 

	dumpPkt(buffer, size);
	memcpy(datagram, buffer, size); 

	// parse header
	type = datagram[0];
	sz   = datagram[1];

	if (sz < 2 || sz > BLE_DATA_MAX)
		return;

	switch(type)
	{
		case TYPE_RAW:
			bleRaw(datagram + 2, sz - 2);
			break;
		case TYPE_DIMMING1:
			bleDimming(datagram + 2, sz - 2);
			break;
		default:
			break;
	}
}

void debug_dimmer()
{
	// fixed dimming
#if 0
	// 0%
	rawValue rv0 = {1, 0};
	bleRaw((byte*)&rv0, sizeof(rawValue));
	delay(3000);
#endif
#if 0
	// 50%
	rawValue rv50 = {1, 64};
	bleRaw((byte*)&rv50, sizeof(rawValue));
	delay(3000);
#endif
#if 0
	// 100%
	rawValue rv100 = {1, 128};
	bleRaw((byte*)&rv100, sizeof(rawValue));
	delay(3000);
#endif

	// continuous dimming
#if 0
	// y = x
	dimValue dv1 = {1, 50, 0, 1, 1, 1, 0};
	bleDimming((byte*)&dv1, sizeof(dimValue));
#endif
#if 0
	// y = 1/128x^2
	dimValue dv2 = {1, 50, 1, 128, 0, 1, 0};
	bleDimming((byte*)&dv2, sizeof(dimValue));
#endif
#if 0
	// y = -1/128x^2 + 2x
	dimValue dv3 = {1, 50, -1, 128, 2, 1, 0};
	bleDimming((byte*)&dv3, sizeof(dimValue));
#endif
}

void loop() 
{
	pktSerial.update();
	 
#if defined(__DEBUG__)
	debug_dimmer();
#endif
}

void dumpPkt(const byte* packet, size_t size)
{
#if defined(__DEBUG__)
	char buf[10] = "";

	Serial.print("DUMP ");

	for(int i=0; i<size; i++)
	{
		sprintf(buf, "[%02X] ", packet[i]);
		Serial.print(buf);
	}

	Serial.println();
#endif
}

void syslog(char *fmt, ... )
{
#if defined(__DEBUG__)
	char buf[128];
	va_list args;
	
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), (const char *)fmt, args);
	va_end(args);

	Serial.println(buf);
#endif
}
