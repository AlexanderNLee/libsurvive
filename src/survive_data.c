//<>< (C) 2016 C. N. Lohr, MOSTLY Under MIT/x11 License.
//
//Based off of https://github.com/collabora/OSVR-Vive-Libre
// Originally Copyright 2016 Philipp Zabel
// Originally Copyright 2016 Lubosz Sarnecki <lubosz.sarnecki@collabora.co.uk>
// Originally Copyright (C) 2013 Fredrik Hultin
// Originally Copyright (C) 2013 Jakob Bornecrantz
//
//But, re-written as best as I can to get it put under an open souce license instead of a forced-source license.
//If there are portions of the code too similar to the original, I would like to know  so they can be re-written.
//All MIT/x11 Licensed Code in this file may be relicensed freely under the GPL or LGPL licenses.

#include "survive_internal.h"
#include <stdint.h>
#include <string.h>

#define POP1  (*(readdata++))
#define POP2  (*(((uint16_t*)((readdata+=2)-2))))
#define POP4  (*(((uint32_t*)((readdata+=4)-4))))

//Pass pointer to end-of-string
int ARCANEPOP( uint8_t ** end, uint8_t * start )
{
	//78
	//a8 03
	uint8_t tnum;
	uint32_t accumulator;

	tnum = (*end)[0];
	accumulator = (tnum&0x7f);
	(*end)--;
	if( ( tnum & 0x80 ) || ( *end == start ) )	return accumulator;

	tnum = (*end)[0];
	accumulator = (accumulator<<7)|(tnum&0x7f);
	(*end)--;
	if( ( tnum & 0x80 ) || ( *end == start ) )	return accumulator;

	tnum = (*end)[0];
	(*end)--;
	accumulator = (accumulator<<7)|(tnum&0x7f);
	if( ( tnum & 0x80 ) || ( *end == start ) )	return accumulator;

	tnum = (*end)[0];
	(*end)--;
	accumulator = (accumulator<<7)|(tnum&0x7f);
	return accumulator;
}


struct LightcapElement
{
	uint8_t sensor_id;
	uint8_t type;
	uint16_t length;
	uint32_t timestamp;
} __attribute__((packed));



static void handle_lightcap( struct SurviveObject * so, struct LightcapElement * le )
{
	struct SurviveContext * ct = so->ctx;

	if( le->type != 0xfe || le->length < 50 ) return;
	//le->timestamp += (le->length/2);
	if( le->length > 900 ) //Pulse longer than 18us? 
	{
		int32_t deltat = (uint32_t)le->timestamp - (uint32_t)ct->last_photo_time;
		if( deltat > 2000 || deltat < -2000 )		//New pulse. (may be inverted)
		{
			ct->last_photo_time = le->timestamp;
			ct->total_photo_time = 0;
			ct->total_photos = 0;
			ct->total_pulsecode_time = 0;
			survive_light_process( so, le->sensor_id, -1, 0, le->timestamp );
		}
		else
		{
			ct->total_pulsecode_time += le->length;
			ct->total_photo_time += deltat;
			ct->total_photos++;
		}
	}
	else if( le->length < 900 && le->length > 50 && ct->total_photos )
	{
		int32_t dl = (ct->total_photo_time/ct->total_photos);
		int32_t tpco = (ct->total_pulsecode_time/ct->total_photos);
		//Adding length 
		int32_t offset_from = le->timestamp - dl - ct->last_photo_time + le->length/2;

		//Long pulse-code from IR flood.
		//Make sure it fits nicely into a divisible-by-500 time.
		int32_t acode = (tpco+125)/250;
		if( acode & 1 ) return;
		acode>>=1;

		survive_light_process( so, le->sensor_id, acode, offset_from, le->timestamp );
	}
	else
	{
		//Runt pulse.
	}
}



static void handle_watchman( struct SurviveObject * w, uint8_t * readdata )
{
	int i;

	uint8_t startread[29];
	memcpy( startread, readdata, 29 );

#if 0
	printf( "DAT:     " );
		for( i = 0; i < 29; i++ )
		{
			printf( "%02x ", readdata[i] );
		}
		printf("\n");
#endif

	uint8_t time1 = POP1;
	uint8_t qty = POP1;
	uint8_t time2 = POP1;
	uint8_t type = POP1;
	qty-=2;
	int propset = 0;
	int doimu = 0;


	if( (type & 0xf0) == 0xf0 )
	{
		propset |= 4;
		//printf( "%02x %02x %02x %02x\n", qty, type, time1, time2 );
		type &= ~0x10;

		if( type & 0x01 )
		{
			qty-=1;
			w->buttonmask = POP1;
			type &= ~0x01;
		}
		if( type & 0x04 ) 
		{
			qty-=1;
			w->axis1 = ( POP1 ) * 128; 
			type &= ~0x04;
		}
		if( type & 0x02 )
		{
			qty-=4;
			w->axis2 = POP2;
			w->axis3 = POP2;
			type &= ~0x02;
		}

		//XXX TODO: Is this correct?  It looks SO WACKY
		type &= 0x7f;
		if( type == 0x68 ) doimu = 1;
		type &= 0x0f;
		if( type == 0x00 && qty ) { type = POP1; qty--; }
	}

	if( type == 0xe1 )
	{
		propset |= 1;
		w->charging = readdata[0]>>7;
		w->charge = POP1&0x7f; qty--;
		w->ison = 1; 
		if( qty )
		{
			qty--;
			type = POP1; //IMU usually follows.
		}
	}

	if( ( ( type & 0xe8 ) == 0xe8 ) || doimu ) //Hmm, this looks kind of yucky... we can get e8's that are accelgyro's but, cleared by first propset.
	{
		propset |= 2;
		survive_imu_process( w, (int16_t *)&readdata[1], (time1<<24)|(time2<<16)|readdata[0], 0 );
		int16_t * k = (int16_t *)readdata+1;
		//printf( "Match8 %d %d %d %d %d %3d %3d\n", qty, k[0], k[1], k[2], k[3], k[4], k[5] );
		readdata += 13; qty -= 13;
		type &= ~0xe8;
		if( qty )
		{
			qty--;
			type = POP1;
		}
	}


	if( qty )
	{
			int j;
		qty++;
		readdata--;
		*readdata = type;
		//Put type back on stack.  It might have changed
		//from above.


#if 0 //Doesn't work!!!!
		int reads = qty/6;

		int leds[6];
		leds[0] = type;
		for( i = 1; i < reads; i++ )
		{
			leds[i] = POP1;
		}
		for( i = 0; i < reads; i++ )
		{
			printf( "%02x: ", leds[i] );
			for( i = 0; i < 5; i++ )
			{
				printf( "%02x ", POP1 );
			}
			printf( "\n" );
		}
#endif

		//What does post data look like?

		//1) code, code, code,  PAAYYLOOAAADDD
		//"code" typically has 2 or fewer bits in word set.
		//  code = 0x00 = expect 5 bytes 
		//  code = 0x08 = expect 5 bytes
		//  code = 0x10 = expect 5 bytes
		//  code = 0x5c = ???? ... 12?
		//  code = 0x91 = ???? ... 25 bytes?


#if 1
		static int lasttime;
		//             good        good          maybe?         probably wrong
		int mytimex = (time1<<24)|(time2<<16)|(readdata[4]<<8)|(readdata[5]);
		int diff = mytimex - lasttime;
		lasttime = mytimex;
		printf( "POST %d: %4d (%02x%02x) - ", propset, qty, time1, time2 );
		for( i = 0; i < qty + 4; i++ )
		{
			printf( "%02x ", readdata[i] );
		}
		printf("\n");
#endif
		//XXX XXX XXX This code is awful.  Rejigger to make go fast.
		uint8_t * end = &readdata[qty-4];
		uint8_t * start = readdata;

		//XXX TODO: Do something clever here to get the MSB for the timecode.  Can do by
		//looking at the difference of the msb of the wrong packet and last OR difference from this
		//and IMU timecode.
		uint32_t imutime = (time1<<24) | (time2<<16);
		uint32_t mytime = (end[1] << 0)|(end[2] << 8)|(end[3] << 16);
		mytime |= (time1<<24);
		int32_t tdiff = mytime - imutime;
		if( tdiff > 0x7fffff )  { mytime -= 0x01000000; }
		if( tdiff < -0x7fffff ) { mytime += 0x01000000; }

		uint32_t  parameters[25];
		int parplace = 0;

		while( end != start )
		{
			uint32_t acpop = ARCANEPOP( &end, start );
			parameters[parplace++] = acpop;
			int remain = end - start + 1;
			if( (parplace+1) / 2 >= remain ) break;
		}

		uint32_t lights[10];
		int lightno = 0;

		while( end >= start )
		{
			lights[lightno++] = end[0];
			end--;
		}

		if( (parplace+1)/2 != lightno )
		{
			struct SurviveContext * ctx = w->ctx;
			SV_INFO( "Watchman code count doesn't match" );
			return;
		}
#if 1
		//This is getting very close.

		const int M_LEDS = 32;
		uint8_t onleds[M_LEDS];
		uint32_t offtimes[M_LEDS];
		memset( onleds, 0, sizeof( onleds ) );
		int k = 0;

		for( i = lightno-1; i >= 0; i-- )
		{
			int led = lights[i]>>3;
			int ledtime = lights[i]&0x07;
			int deltaA = k?parameters[k*2-1]:0;
			int deltaB = parameters[k*2+0];

			k++;

			onleds[led] = ledtime+1;
			offtimes[led] = mytime;
			printf( "%d %d %d %d\n", led, ledtime, deltaA, deltaB );

			if( deltaA )
			{
				mytime -= deltaA;

				for( j = 0; j < M_LEDS; j++ )
				{
					if( onleds[j] )
					{
						onleds[j]--;
						if( !onleds[j] )
						{
							//Got a light event.
							struct LightcapElement le;
							le.type = 0xfe;
							le.sensor_id = j;
							le.timestamp = mytime;
							if( offtimes[j] - mytime > 65535 ) 
							{
								printf( "OFLOW: LED %d @ %d, len %d\n", j, mytime, offtimes[j] - mytime );
							}
							else
							{
								le.length = offtimes[j] - mytime;
								handle_lightcap( w, &le );
								printf( "Light Event: LED %d @ %d, len %d\n", j, mytime, le.length );
							}
						}
					}
				}
			}
			mytime -= deltaB;
			for( j = 0; j < M_LEDS; j++ )
			{
				if( onleds[j] )
				{
					onleds[j]--;
					if( !onleds[j] )
					{
						//Got a light event.
						struct LightcapElement le;
						le.type = 0xfe;
						le.sensor_id = j;
						le.timestamp = mytime;
						if( offtimes[j] - mytime > 65535 ) 
						{
							printf( "OFLOW: LED %d @ %d, len %d\n", j, mytime, offtimes[j] - mytime );
						}
						else
						{
							le.length = offtimes[j] - mytime;
							handle_lightcap( w, &le );
							printf( "Light Event: LED %d @ %d, len %d\n", j, mytime, le.length );
						}
					}
				}
			}
		}

		for( j = 0; j < M_LEDS; j++ )
		{
			if( onleds[j] )
			{
				fprintf(stderr, "ERROR: Too many LEDs found (%d = %d)\n", j, onleds[j] );
			}
		}

#endif


/*
		static int olddel = 0;
		int lightmask = lights[0];
		//Operting on mytime
		printf( "Events (%10d): ", mytime );
		for( i = 0; i < lightno; i++ )
		{
			int deltaA = parameters[parplace-i*2-1];
			int deltaB = -1;
			if( parplace-i*2-2 >= 0 )
				deltaB = parameters[parplace-i*2-2];

			printf( "%02x/(%5d/%5d)", lightmask, deltaA, deltaB );
			if( lightno-i-2 >= 0 )
			lightmask = lights[i+1];
		}
		printf( "\n");

*/
		//What I think I know:
		//	2 pulses: mytime pulse? deltime? deltime? pulse? deltime?
		//	3 pulses: mytime pulse? deltime? deltime? pulse? deltime? deltime? pulse deltime?
		// Maybe pulse codes are change in masks?
		//One light on then off. Events (1358180290): 50/(  112/ 1714)50/(  107/   -1)
		// LED 50 / LED 60
		//  5a/(7 >< 90)60/( 7 <> 1719)5a/(7/90)60/(7/-1)
		// 50 then 
		// 7 later) 60 on
		// 90 later, 60 only
		// 7 later, 60 off.
		// 1719 later, 50 on
		// 7 later, 60 on.
		// 90 later, 50 off
		// 7 later, 60 off.

		//Notes: All LEDs are divisble by 8.
		//That leaves us 3 bits earlier in the message.
		//
		//

		//Assumptions:  Bit 0 1
//XXX DISCARD BETWEEN HERE
		//More details.  Example:
		//  40 only / 40 and 48 /  48 only
		//   49/(  139/  102)41/(  130/   -1)
		//  48 only / 48 and 40 /  40 only
		//   41/(  129/  102)49/(  132/   -1)
		//
		//  00 then 08+00 then 08 only
		//    09/(  133/  100)01/(  135/   -1)
		//  08 then 08+00 then 00 only
		//    01/(  133/  102)09/(  133/   -1)
		//
		// 10 then 10+00+08 then 00+08
		//    0a/(  107/   20)73/(   93/   17)11/(  132/   -1)
		// 08+00 then 10+08+00  then 10
		//    6a/(   34/   35)12/(   86/  125)0a/(   20/   -1)
		//
		//  08 then 10+08 then 10
		//    11/(  136/   97)09/(  136/   -1)

		//  08 then nothing then 10
		// 10/(  104/  161)08/(  102/   -1)
		// 10/(  105/  425)08/(  102/   -1)  (LONGER DELAY BETWEEN)
		// 10/(  369/  423)08/(  102/   -1)  (LONGER FIRST PULSE)
		// 40 -> 48 -> 50 50/(  523/ 2260)48/(  520/ 2260)40/(  873/   -1)
//AND HERE!!! XXX DISCARD



		//I think the format is:
		// mytime = start of events
		//   mask = LEDs on.
		//     parameter = delta
		//   mask = Led mask switching
/*
		static int olddel = 0;
		int lightmask = lights[0];

		//
		//
		//

		//Operting on mytime
		printf( "Events (%10d): ", mytime );
		for( i = 0; i < lightno; i++ )
		{
			int deltaA = parameters[parplace-i*2-1];
			int deltaB = -1;
			if( parplace-i*2-2 >= 0 )
				deltaB = parameters[parplace-i*2-2];

			printf( "%02x/(%5d/%5d)", lightmask, deltaA, deltaB );
			if( lightno-i-2 >= 0 )
			lightmask = lights[i+1];
		}
		printf( "\n");
*/
		//Three distinct motions. 
		// 40 nothing (long wait) 48 nothing 50 nothing (long pulse)
		//    40/(  524/ 2262)48/(  522/  570)50/(  874/   -1)
		// 40, 40+48, 48 only.
		//   41/(  529/  517)49/(  875/   -1)
		// 48, 48+40, 40 only
		//   49/(  525/  518)41/(  877/   -1)
		// 48 then 48+40 then 48 only  //XXX SERIOUSLY????
		//   40/(  526/  524)4a/(  869/   -1)
		// 48 then 48+50 then 50 only.
		//   49/(  528/  518)51/(  881/   -1)
		// 50 then 48+50 then 48 only.
 		//    51/(  528/  520)49/(  523/   -1)
		// 48 then 48+50 then 50 only.
		//    49/(  527/  518)51/(  532/   -1)
		// 48 then 48+50 then 48 only //INCORRECT BELOW HERE XXX DO NOT TRUST!!!
		//     50/(  527/  524)4a/(  515/   -1)  //No, really... wat?
		// 50 then 48+50 then 50 only
		//     48/(  529/  521)52/(  524/   -1)  // oh come on!
		// 50 then 48+50 then 50 only then 50+40 then 50 only.
		//     40/( 5364/ 2292)50/( 5363/283869)48/(  526/  521)52/(  522/   -1)






/*
		for( i = 0; i < lightno; i++ )
		{
			struct LightcapElement le;
			le.sensor_id = lights[lightno-i-1];
			le.type = 0xfe; 
			le.length = parameters[parplace-i*2-1];
			le.timestamp = mytime;
			handle_lightcap( w, &le );

			int delta = le.timestamp - olddel;
			olddel = le.timestamp;

			printf( "LIGHTCAP: %02x %3d %10lu %d\n", le.sensor_id, le.length, le.timestamp, delta );

			int pl = parplace-i*2-2;
			if( pl>=0 )
				mytime += parameters[parplace-i*2-2];
		}
*/


#if 0
		printf( "  SRR: " );
		for( i = 0; i < 29; i++ )
		{
			printf( "%02x ", startread[i] );
		}
		printf( "\n" );
#endif
	}

	return;
	//NO, seriously, there is something wacky going on here.
//	else if( type == 0xe8 && sensor_id == 15 )
//	{
//		printf( "IMU\n" );
//	}

/*
	{
		printf( "PSIDPIN:%3d ", qty );
		w->charging = readdata[0]>>7;
		w->charge = readdata[0]&0x7f;
		w->ison = 1;
		printf( "%02x/%02x/%02x/%02x: ", type, qty, time1, time2 );
		for( i = 0; i < 25; i++ )
		{
			printf( "%02x ", readdata[i] );
		}
		printf("\n");

/*
		//WHAT IS THIS???
		printf( "%02x/%02x/%02x/%02x: ", type, sensor_id, time1, time2 );
		for( i = 0; i < 25; i++ )
		{
			printf( "%02x ", readdata[i] );
		}
		printf("\n");*/
}


void survive_data_cb( struct SurviveUSBInterface * si )
{
	int size = si->actual_len;
	struct SurviveContext * ctx = si->ctx;
#if 0
	int i;
	printf( "%16s: %d: ", si->hname, len );
	for( i = 0; i < size; i++ )
	{
		printf( "%02x ", si->buffer[i] );
	}
	printf( "\n" );
	return;
#endif 

	int iface = si->which_interface_am_i;
	uint8_t * readdata = si->buffer;

	int id = POP1;
//	printf( "%16s Size: %2d ID: %d / %d\n", si->hname, size, id, iface );


	switch( si->which_interface_am_i )
	{
	case USB_IF_HMD:
	{
		struct SurviveObject * headset = &ctx->headset;
		readdata+=2;
		headset->buttonmask = POP1;		//Lens
		headset->axis2 = POP2;			//Lens Separation
		readdata+=2;
		headset->buttonmask |= POP1;	//Button
		readdata+=3;
		readdata++;						//Proxchange, No change = 0, Decrease = 1, Increase = 2
		readdata++;
		headset->axis3 = POP2;			//Proximity  	<< how close to face are you?  Less than 80 = not on face.
		headset->axis1 = POP2;			//IPD   		<< what is this?
		headset->ison = 1;
		break;
	}
	case USB_IF_LIGHTHOUSE:
	{
		int i;
		//printf( "%d -> ", size );
		for( i = 0; i < 3; i++ )
		{
/*
			for( i = 0; i < 17; i++ )
			{
				printf( "%02x ", readdata[i] );
			}
			printf( "\n" );
*/
			//handle_lightdata( (struct LightpulseStructure *)readdata );
			int16_t * acceldata = (int16_t*)readdata;
			readdata += 12;
			uint32_t timecode = POP4;
			uint8_t code = POP1;
			//printf( "%d ", code );
			int8_t cd = code - ctx->oldcode;

			if( cd > 0 )
			{
				ctx->oldcode = code;
				survive_imu_process( &ctx->headset, acceldata, timecode, code );
			}
		}

		//DONE OK.
		break;
	}
	case USB_IF_WATCHMAN1:
	case USB_IF_WATCHMAN2:
	{
		struct SurviveObject * w = &ctx->watchman[si->which_interface_am_i-USB_IF_WATCHMAN1];
		if( id == 35 )
		{
			handle_watchman( w, readdata);
		}
		else if( id == 36 )
		{
			handle_watchman( w, readdata);
			handle_watchman( w, readdata+29 );
		}
		else if( id == 38 )
		{
			w->ison = 0;
		}
		else
		{
			SV_INFO( "Unknown watchman code %d\n", id );
		}
		break;
	}
	case USB_IF_LIGHTCAP:
	{
		//Done!
		int i;
		for( i = 0; i < 7; i++ )
		{
			handle_lightcap( &ctx->headset, (struct LightcapElement*)&readdata[i*8] );
		}
		break;
	}
	}
}

