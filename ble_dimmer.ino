/* Ref.
 * http://playground.arduino.cc/Code/ACPhaseControl
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <kyet@me.com> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.         Isaac K. Ko
 * ----------------------------------------------------------------------------
 */

#include <stdarg.h>
#include <TimerOne.h>
#include <SoftwareSerial.h>
#include <PacketSerial.h>

/* Device type */
#define SWITCH       0x01
#define DIMMER       0x02
const byte devType = DIMMER;

// Undefine for release
#define __DEBUG__

/* Data type */
#define TYPE_RAW     0x01
#define TYPE_DIMMING 0x02
#define TYPE_UNDEF6  0x04
#define TYPE_UNDEF5  0x08
#define TYPE_UNDEF4  0x10
#define TYPE_UNDEF3  0x20
#define TYPE_UNDEF2  0x40
#define TYPE_UNDEF1  0x80

/* For dimmer */
#define ZERO_CROSS_INT   0   // For zero crossing detect (int0 is pin 2)
#define HZ               60  // 60 for the most America, Korea, Taiwan, Phillippines
                             // 50 for China, Europe, Japan, and rest of.

/* For Bluetooth */
#define BLE_RX        10     // Serial pin: connect reverse order
#define BLE_TX        11
#define BLE_DATA_MAX  20     // Payload size

/* Outlet pin */
const byte outlet[] = {3, 5};
const byte nOutlet  = sizeof(outlet) / sizeof(outlet[0]);

/* For dimmer */
/* NOTE: sec to us, hertz to frequency, half cycle
 * Approx. 8,333us for 60Hz. 10,000us for 50Hz.
 */
const byte range  = 128;
const byte period = (byte)((1000.0 * 1000.0) / (HZ * 2 * range));
// The time to idle low on the outlet[]
volatile byte dimming[nOutlet] = {0};
volatile byte crossing = 0;

SoftwareSerial bleSerial(BLE_RX, BLE_TX);
PacketSerial   pktSerial;

void setup() 
{
	Serial.begin(9600);

	// Wait for serial port to connect
	// Needed for native USB port only
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
		Timer1.attachInterrupt(triggerTriac, period);
	}
}

void zeroCrossInt()
{
	crossing = 1;
}

/* NOTE: Do not use delay() in interrupt. It depends on interrupt itself.
 * Instead, use delayMicroseconds() that run busy loop(NOP).
 */
void triggerTriac()
{
	static byte cnt = 0;

	if (crossing)
	{
		if (cnt++ == dimming[0])
		{
			digitalWrite(outlet[0], HIGH);
			delayMicroseconds(10);  // triac switching delay
			digitalWrite(outlet[0], LOW);

			crossing = 0;
			cnt = 0;
		}
	}
}

// Event handler
typedef struct _portValue {
	byte port;
	byte value;
} portValue;

void bleRaw(byte *data, byte sz)
{
	portValue *pv = NULL;

	// Sanity check
	if (sz < 2)             { return; }
	if ((sz % 2) != 0)      { return; }
	if ((sz / 2) > nOutlet) { return; }

	dumpPkt(data, sz);

	for (int i=0; i<sz; i+=2)
	{
		pv = (portValue *)(data + i);

		if (pv->port == 0) { continue; }
		pv->port--;

		if (devType == SWITCH)
		{
			if (pv->value >= 0x80)
			{
				digitalWrite(outlet[pv->port], HIGH);
				syslog("LED %d(%dpin) ON(0x%02X)", 
						pv->port, outlet[pv->port], pv->value);
			}
			else
			{
				digitalWrite(outlet[pv->port], LOW);
				syslog("LED %d(%dpin) OFF(0x%02X)",
						pv->port, outlet[pv->port], pv->value);
			}
		}
		else
		{
			// FIXME
			//AnalogWrite(outlet[pv->port], pv->value);
			syslog("LED %d(%dpin) Value(0x%02X)",
					pv->port, outlet[pv->port], pv->value);
		}
	}
}

void bleDimming(byte *data, byte sz)
{
	
	
}

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

	// Parse header
	type = datagram[0];
	sz   = datagram[1];

	if (sz < 2 || sz > BLE_DATA_MAX)
		return;

	switch(type)
	{
		case TYPE_RAW:
			bleRaw(datagram + 2, sz - 2);
			break;
		case TYPE_DIMMING:
			bleDimming(datagram + 2, sz - 2);
			break;
		default:
			break;
	}
}

void loop() 
{
#if 0
	pktSerial.update();
#endif
	 
	// available range: 10 ~ 124
	for (byte i=10; i<=(range-10); i+=1)
	{
		dimming[0] = (range-i);
		delay(30);
	}
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
