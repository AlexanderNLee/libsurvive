
//Based off of vl_hid_reports (HTC Vive USB HID reports).  Who would license a header un the GPL???

#include "survive_internal.h"
#include <stdint.h>

#define POP1  (*(readdata++))
#define POP2  (*(((uint16_t*)((readdata+=2)-2))))
#define POP4  (*(((uint32_t*)((readdata+=4)-4))))



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
	uint8_t time1 = POP1;
	uint8_t qty = POP1;
	uint8_t time2 = POP1;
	uint8_t type = POP1;
	int i;
	qty-=2;
	int propset = 0;

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
			w->axis1 = ( POP1 ) * 256; 
			type &= ~0x04;
		}
		if( type & 0x02 )
		{
			qty-=4;
			w->axis2 = POP2;
			w->axis3 = POP2;
			type &= ~0x02;
		}

		//XXX TODO: Maybe more data is here?
		if( type == 0xe0 )
		{
			type = 0x00;
		}
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

	if( ( type & 0xe8 ) == 0xe8 )
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
		qty++;
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

#if 0
		printf( "POST %d: %4d %02x (%02x%02x) - ", propset, qty, type, time1, time2 );
		for( i = 0; i < qty; i++ )
		{
			printf( "%02x ", readdata[i] );
		}
		printf("\n");
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
		for( i = 0; i < 3; i++ )
		{
			//handle_lightdata( (struct LightpulseStructure *)readdata );
			int16_t * acceldata = (int16_t*)readdata;
			readdata += 12;
			uint32_t timecode = POP4;
			survive_imu_process( &ctx->headset, acceldata, timecode, POP1 );
		}
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


/*
 * 
 * Copyright 2016 Philipp Zabel
 * SPDX-License-Identifier:	LGPL-2.0+
 */
#if 0

struct vive_headset_power_report {
	__u8 id;
	__le16 type;
	__u8 len;
	__u8 unknown1[9];
	__u8 reserved1[32];
	__u8 unknown2;
	__u8 reserved2[18];
} __attribute__((packed));

struct vive_headset_mainboard_device_info_report {
	__u8 id;
	__le16 type;
	__u8 len;
	__be16 edid_vid;
	__le16 edid_pid;
	__u8 unknown1[4];
	__le32 display_firmware_version;
	__u8 unknown2[48];
} __attribute__((packed));

struct vive_firmware_version_report {
	__u8 id;
	__le32 firmware_version;
	__le32 unknown1;
	__u8 string1[16];
	__u8 string2[16];
	__u8 hardware_version_micro;
	__u8 hardware_version_minor;
	__u8 hardware_version_major;
	__u8 hardware_revision;
	__le32 unknown2;
	__u8 fpga_version_minor;
	__u8 fpga_version_major;
	__u8 reserved[13];
} __attribute__((packed));

struct vive_headset_imu_sample {
        __s16 acc[3];
        __s16 rot[3];
	__le32 time_ticks;
	__u8 seq;
} __attribute__((packed));

struct vive_headset_imu_report {
	__u8 report_id;
	struct vive_headset_imu_sample samples[3];
} __attribute__((packed));



struct vive_controller_analog_trigger_message {
	__u8 squeeze;
	__u8 unknown[4];
} __attribute__((packed));

struct vive_controller_button_message {
	__u8 buttons;
} __attribute__((packed));

struct vive_controller_touch_move_message {
	__le16 pos[2];
	__u8 unknown[4];
} __attribute__((packed));

struct vive_controller_touch_press_message {
	__u8 buttons;
	__le16 pos[2];
	__u8 unknown[4];
} __attribute__((packed));

struct vive_controller_imu_message {
	__u8 time3;
	__le16 accel[3];
	__le16 gyro[3];
	__u8 unknown[4];
} __attribute__((packed));

struct vive_controller_ping_message {
	__u8 charge : 7;
	__u8 charging : 1;
	__u8 unknown1[2];
	__le16 accel[3];
	__le16 gyro[3];
	__u8 unknown2[5];
} __attribute__((packed));

struct vive_controller_message {
	__u8 time1;
	__u8 sensor_id;
	__u8 time2;
	__u8 type;
	union {
		struct vive_controller_analog_trigger_message analog_trigger;
		struct vive_controller_button_message button;
		struct vive_controller_touch_move_message touch_move;
		struct vive_controller_touch_press_message touch_press;
		struct vive_controller_imu_message imu;
		struct vive_controller_ping_message ping;
		__u8 unknown[25];
	};
} __attribute__((packed));

struct vive_controller_report1 {
	__u8 report_id;
	struct vive_controller_message message;
} __attribute__((packed));

struct vive_controller_report2 {
	__u8 report_id;
	struct vive_controller_message message[2];
} __attribute__((packed));

struct vive_headset_lighthouse_pulse2 {
        uint8_t sensor_id;
        uint16_t length;
        uint32_t timestamp;
} __attribute__((packed));

struct vive_headset_lighthouse_pulse_report2 {
	__u8 report_id;
	struct vive_headset_lighthouse_pulse2 samples[9];
} __attribute__((packed));

struct vive_controller_poweroff_report {
	__u8 id;
	__u8 command;
	__u8 len;
	__u8 magic[4];
} __attribute__((packed));


#endif
