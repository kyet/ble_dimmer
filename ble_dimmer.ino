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

// for dimmer
//#define DIM_RANGE      256
#define DIM_RANGE        128
#define ZERO_CROSS_INT   0   // For zero crossing detect (int0 is pin 2)
#define HZ               60  // 60 for the most America, Korea, Taiwan, Phillippines
                             // 50 for China, Europe, Japan, and rest of.

//SoftwareSerial bleSerial(10, 11); // Rx, Tx (Connect reverse order)
//PacketSerial   pktSerial;

//const int outlet[] = {2, 4, 7, 8, 12, 13};
//const int outlet[] = {3, 5, 6, 9};
//const int outlet[] = {3, 5};
const int outlet[] = {3, 5};
const int nOutlet = sizeof(outlet) / sizeof(outlet[0]);

// For dimmer
// Approx. 8,333us for 60Hz. 10,000us for 50Hz.
// sec to us, hertz to frequency, half cycle
const unsigned int period = 1000 * 1000 / HZ / 2;

// The time to idle low on the outelt[]
volatile unsigned int dimming[nOutlet] = {0}; // FIXME: change to byte after adjust range 

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

//	bleSerial.begin(9600);
//
//	pktSerial.setPacketHandler(&bleParser);
//	pktSerial.begin(&bleSerial);

#if defined(DIMMING)
	pinMode(ZERO_CROSS_INT, INPUT);
	attachInterrupt(ZERO_CROSS_INT, zeroCrossInt, RISING);  
#endif

	Timer1.attachInterrupt(triggerTriac, period/256);
}

volatile byte crossing = 0;
//int dim = 0;
void zeroCrossInt()
{
//	digitalWrite(outlet[0], LOW);
//	Timer1.restart();
//	Timer1.attachInterrupt(triggerTriac, dimming[0]);

	crossing = 1;
#if 0
	if (dimming[0] <= 100)
		digitalWrite(outlet[0], LOW);
	else if (dimming[0] >= 8000)
		digitalWrite(outlet[0], HIGH);	
	else
		Timer1.attachInterrupt(triggerTriac, dimming[0]/100);
#endif

#if 0
	for (int i=1; i<=DIM_RANGE; i++)
	{
		for (int j=0; j<nOutlet; j++)
		{
			if ((DIM_RANGE-i) == dimming[j])
			{
				// Turn on using Triac
				digitalWrite(outlet[j], HIGH);
				delayMicroseconds(10);        // Triac turning on delay
				digitalWrite(outlet[j], LOW);

				syslog("LED %d(%dpin) Turn ON at %d(%dus)",
						j+1, outlet[j], (i-1), (i-1)*tDim);
			}
		}

		// Turn off by just wait
		delayMicroseconds(tDim);
	}
#endif
#if 0
	delayMicroseconds(65 * (128 - dim));  // Off cycle
	//delayMicroseconds(33*(256-dim));  // Off cycle
	digitalWrite(3, HIGH);       // triac firing
	delayMicroseconds(10);     // triac On propogation delay
	digitalWrite(3, LOW);        // triac Off
#endif
}

/* NOTE: Do not use delay() in interrupt. It depends on interrupt itself.
 * Instead, use delayMicroseconds() that run busy loop(nop).
 */
void triggerTriac ()
{
	static unsigned int cnt = 0;
	//Timer1.detachInterrupt();
	//digitalWrite(outlet[0], HIGH);
	//delayMicroseconds(10);  // triac On propogation delay
	//digitalWrite(outlet[0], LOW);
	if (crossing)
	{
		cnt++;
		if (cnt++ == 4000/256)
		{
			digitalWrite(outlet[0], HIGH);
			delayMicroseconds(10);  // triac On propogation delay
			digitalWrite(outlet[0], LOW);
			//syslog("fire! cnt(%d)", cnt-1);
			crossing = 0;
			cnt = 0;
		}
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
	//static int thres1 = 0;
	//static int thres2 = 10;
	//dim = 124;
	// available range: 10 ~ 124
	//pktSerial.update();
	 //for (int i=1; i <= 256; i+=1)
//	 for (int i=thres1; i <= thres2; i++)
//	 {
//		 dim=i;
//		 delay(1000);
//	 }

#if 0
	 for (int i=thres2; i >= thres1; i--)
	 {
		 dim=i;
		 delay(10);
	 }
#endif

	// test code
//	digitalWrite(outlet[0], HIGH);
//	delayMicroseconds(10);        // Triac turning on delay
//	digitalWrite(outlet[0], LOW);

	 // re scale the value from hex to uSec 
//	 dimming[0] = period - map(hexValue, 0, 256, 0, period);
	 //dimming[0] = period - map(hexValue, 0, 256, 0, period);
	 
	 for (int i=0; i<=8300; i+=100)
	 {
		 dimming[0] = i;
		 delay(30);
	 }
	 for (int i=8300; i>=0; i-=100)
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
