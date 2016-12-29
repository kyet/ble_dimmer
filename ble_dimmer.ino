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
//#include <SoftwareSerial.h>
//#include <PacketSerial.h>

// Device type
//#define SWITCH
#define DIMMING

// Undefine for release
#define __DEBUG__

#define BLE_DATA_MAX 20

#define TYPE_RAW     0x01
#define TYPE_DIMMING 0x02

#define TYPE_UNDEF3  0x40
#define TYPE_UNDEF2  0x80
#define TYPE_UNDEF1  0xFF

/* For dimmer */
//#define DIM_RANGE      256
//#define DIM_RANGE        128
#define ZERO_CROSS_INT   0   // For zero crossing detect (int0 is pin 2)
#define HZ               60  // 60 for the most America, Korea, Taiwan, Phillippines
                             // 50 for China, Europe, Japan, and rest of.

//SoftwareSerial bleSerial(10, 11); // Rx, Tx (Connect reverse order)
//PacketSerial   pktSerial;

const int outlet[] = {3, 5};
const int nOutlet  = sizeof(outlet) / sizeof(outlet[0]);

/* For dimmer */
/* NOTE: sec to us, hertz to frequency, half cycle
 * Approx. 8,333us for 60Hz. 10,000us for 50Hz.
 */
const unsigned int range = 128;
//const unsigned int period = ((1000 * 1000) / HZ) / 2 / range; // FIXME: this set to 0.. 
const unsigned int period = 65;

// The time to idle low on the outlet[]
volatile uint8_t dimming[nOutlet] = {0};

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

#if 0
	bleSerial.begin(9600);

	pktSerial.setPacketHandler(&bleParser);
	pktSerial.begin(&bleSerial);
#endif

#if defined(DIMMING)
	pinMode(ZERO_CROSS_INT, INPUT);
	attachInterrupt(ZERO_CROSS_INT, zeroCrossInt, RISING);  
	Timer1.initialize();
	Timer1.attachInterrupt(triggerTriac, period);
#endif

}

volatile uint8_t crossing = 0;
void zeroCrossInt()
{
	crossing = 1;
}

/* NOTE: Do not use delay() in interrupt. It depends on interrupt itself.
 * Instead, use delayMicroseconds() that run busy loop(NOP).
 */
void triggerTriac()
{
	static uint8_t cnt = 0;

	if (crossing)
	{
		if (cnt == (range-dimming[0]))
		{
			digitalWrite(outlet[0], HIGH);
			delayMicroseconds(10);  // triac switching delay
			digitalWrite(outlet[0], LOW);

			crossing = 0;
			cnt = 0;
		}
		cnt++;
	}
}

// Event handler
typedef struct _portValue {
	uint8_t port;
	uint8_t value;
} portValue;

void bleRaw(uint8_t *data, uint8_t sz)
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

#if defined(SWITCH)
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
#else
		//AnalogWrite(outlet[pv->port], pv->value);
		syslog("LED %d(%dpin) Value(0x%02X)",
				pv->port, outlet[pv->port], pv->value);
#endif
	}
}

void bleDimming(uint8_t *data, uint8_t sz)
{
	
	
}

void bleParser(const uint8_t* buffer, size_t size)
{
	uint8_t datagram[BLE_DATA_MAX] = {0};
	uint8_t type, sz;
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
	//pktSerial.update();
	 
	// available range: 10 ~ 124
	for (int i=10; i<=(range-10); i+=1)
	{
		dimming[0] = i;
		delay(30);
	}
}

void dumpPkt(const uint8_t* packet, size_t size)
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
