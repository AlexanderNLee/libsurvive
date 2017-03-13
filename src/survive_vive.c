//Unofficial driver for the official Valve/HTC Vive hardware.
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

#include <survive.h>
#include <jsmn.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

struct SurviveViveData;

const short vidpids[] = {
	0x0bb4, 0x2c87, 0, //The main HTC HMD device
	0x28de, 0x2000, 0, //Valve lighthouse
	0x28de, 0x2101, 0, //Valve Watchman
	0x28de, 0x2101, 1, //Valve Watchman
	0x28de, 0x2022, 0, //HTC Tracker
}; //length MAX_USB_INTERFACES*2

const char * devnames[] = {
	"HMD",
	"Lighthouse",
	"Watchman 1",
	"Watchman 2",
	"Tracker 0",
}; //length MAX_USB_INTERFACES


#define USB_DEV_HMD			0
#define USB_DEV_LIGHTHOUSE	1
#define USB_DEV_WATCHMAN1	2
#define USB_DEV_WATCHMAN2	3
#define USB_DEV_TRACKER0	4
#define MAX_USB_DEVS		5


#define USB_IF_HMD			0
#define USB_IF_LIGHTHOUSE 	1
#define USB_IF_WATCHMAN1	2
#define USB_IF_WATCHMAN2	3
#define USB_IF_TRACKER0		4
#define USB_IF_LIGHTCAP		5
#define MAX_INTERFACES		6

typedef struct SurviveUSBInterface SurviveUSBInterface;
typedef struct SurviveViveData SurviveViveData;

typedef void (*usb_callback)( SurviveUSBInterface * ti );

struct SurviveUSBInterface
{
	SurviveViveData * sv;
	SurviveContext * ctx;

	struct libusb_transfer * transfer;
	SurviveObject * assoc_obj;
	int actual_len;
	uint8_t buffer[INTBUFFSIZE];
	usb_callback cb;
	int which_interface_am_i;	//for indexing into uiface
	const char * hname;			//human-readable names
};

struct SurviveViveData
{
	SurviveContext * ctx;

	//XXX TODO: UN-STATICIFY THIS.
	SurviveUSBInterface uiface[MAX_INTERFACES];
	//USB Subsystem
	struct libusb_context* usbctx;
	struct libusb_device_handle * udev[MAX_USB_DEVS];

};

void survive_data_cb( SurviveUSBInterface * si );

//USB Subsystem 
void survive_usb_close( SurviveContext * t );
int survive_usb_init( SurviveViveData * sv, SurviveObject * hmd, SurviveObject *wm0, SurviveObject * wm1, SurviveObject * tr0 );
int survive_usb_poll( SurviveContext * ctx );
int survive_get_config( char ** config, SurviveViveData * ctx, int devno, int interface, int send_extra_magic );
int survive_vive_send_magic(struct SurviveContext * ctx, void * drv, int magic_code, void * data, int datalen );


static void handle_transfer(struct libusb_transfer* transfer)
{
	struct SurviveUSBInterface * iface = transfer->user_data;
	struct SurviveContext * ctx = iface->ctx;

	if( transfer->status != LIBUSB_TRANSFER_COMPLETED )
	{
		SV_ERROR("Transfer problem %d with %s", transfer->status, iface->hname );
		SV_KILL();
		return;
	}

	iface->actual_len = transfer->actual_length;
	iface->cb( iface );

	if( libusb_submit_transfer(transfer) )
	{
		SV_ERROR( "Error resubmitting transfer for %s", iface->hname );
		SV_KILL();
	}
}



static int AttachInterface( SurviveViveData * sv, SurviveObject * assocobj, int which_interface_am_i, libusb_device_handle * devh, int endpoint, usb_callback cb, const char * hname )
{
	SurviveContext * ctx = sv->ctx;
	SurviveUSBInterface * iface = &sv->uiface[which_interface_am_i];
	iface->ctx = ctx;
	iface->sv = sv;
	iface->which_interface_am_i = which_interface_am_i;
	iface->assoc_obj = assocobj;
	iface->hname = hname;
	struct libusb_transfer * tx = iface->transfer = libusb_alloc_transfer(0);
	iface->cb = cb;
	//printf( "%p %d %p %p\n", iface, which_interface_am_i, tx, devh );

	if (!iface->transfer)
	{
		SV_ERROR( "Error: failed on libusb_alloc_transfer for %s", hname );
		return 4;
	}

	libusb_fill_interrupt_transfer( tx, devh, endpoint, iface->buffer, INTBUFFSIZE, handle_transfer, iface, 0);

	int rc = libusb_submit_transfer( tx );
	if( rc )
	{
		SV_ERROR( "Error: Could not submit transfer for %s (Code %d)", hname, rc );
		return 6;
	}

	return 0;
}

/*
static void debug_cb( struct SurviveUSBInterface * si )
{
	int i;
	int len = si->actual_len;
	printf( "%16s: %d: ", si->hname, len );
	for( i = 0; i < len; i++ )
	{
		printf( "%02x ", si->buffer[i] );
	}
	printf( "\n" );
}*/

//XXX TODO: Redo this subsystem for setting/updating feature reports.

static inline int update_feature_report(libusb_device_handle* dev, uint16_t interface, uint8_t * data, int datalen ) {
//	int xfer;
//	int r = libusb_interrupt_transfer(dev, 0x01, data, datalen, &xfer, 1000);
//	printf( "XFER: %d / R: %d\n", xfer, r );
//	return xfer;
	return libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
		0x09, 0x300 | data[0], interface, data, datalen, 1000 );
}


static inline int getupdate_feature_report(libusb_device_handle* dev, uint16_t interface, uint8_t * data, int datalen ) {

	int ret = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_IN,
		0x01, 0x300 | data[0], interface, data, datalen, 1000 );
	if( ret == -9 ) return -9;
    if (ret < 0)
        return -1;
	return ret;
}



static inline int hid_get_feature_report_timeout(libusb_device_handle* device, uint16_t interface, unsigned char *buf, size_t len )
{
	int ret;
	uint8_t i = 0;
    for (i = 0; i < 100; i++)
	{
        ret = getupdate_feature_report(device, interface, buf, len);
		if( ret != -9 && ( ret != -1 || errno != EPIPE ) ) return ret;
		usleep( 1000 );
	}

	return -1;
}


int survive_usb_init( struct SurviveViveData * sv, struct SurviveObject * hmd, struct SurviveObject *wm0, struct SurviveObject * wm1, struct SurviveObject * tr0 )
{
	struct SurviveContext * ctx = sv->ctx;
	int r = libusb_init( &sv->usbctx );
	if( r )
	{
		SV_ERROR( "libusb fault %d\n", r );
		return r;
	}

	int i;
	int16_t j;
	libusb_device** devs;
	int ret = libusb_get_device_list(sv->usbctx, &devs);

	if( ret < 0 )
	{
		SV_ERROR( "Couldn't get list of USB devices %d", ret );
		return ret;
	}


	//Open all interfaces.
	for( i = 0; i < MAX_USB_DEVS; i++ )
	{
		libusb_device * d;
		int vid = vidpids[i*3+0];
		int pid = vidpids[i*3+1];
		int which = vidpids[i*3+2];

		int did;
		for( did = 0; d = devs[did]; did++ )
		{
			struct libusb_device_descriptor desc;

			int ret = libusb_get_device_descriptor( d, &desc);
			if (ret < 0) {
				continue;
			}

			if( desc.idVendor == vid && desc.idProduct == pid)
			{
				if( which == 0 ) break;
				which--;
			}
		}

		if( d == 0 )
		{
			SV_INFO( "Did not find device %s (%04x:%04x.%d)", devnames[i], vid, pid, which );
			sv->udev[i] = 0;
			continue;
		}

		struct libusb_config_descriptor *conf;
		ret = libusb_get_config_descriptor(d, 0, &conf);
		if( ret )
			continue;
		ret = libusb_open(d, &sv->udev[i]);

		if( !sv->udev[i] || ret )
		{
			SV_ERROR( "Error: cannot open device \"%s\" with vid/pid %04x:%04x", devnames[i], vid, pid );
			return -5;
		}

		libusb_set_auto_detach_kernel_driver( sv->udev[i], 1 );
		for (j = 0; j < conf->bNumInterfaces; j++ )
		{
#if 0
		    if (libusb_kernel_driver_active(sv->udev[i], j) == 1) {
		        ret = libusb_detach_kernel_driver(sv->udev[i], j);
		        if (ret != LIBUSB_SUCCESS) {
		            SV_ERROR("Failed to unclaim interface %d for device %s "
		                    "from the kernel.", j, devnames[i] );
		            libusb_free_config_descriptor(conf);
		            libusb_close(sv->udev[i]);
		            continue;
		        }
		    }
#endif

			if( libusb_claim_interface(sv->udev[i], j) )
			{
				SV_ERROR( "Could not claim interface %d of %s", j, devnames[i] );
				return -9;
			}
		}

		SV_INFO( "Successfully enumerated %s (%d, %d)", devnames[i], did, conf->bNumInterfaces );
	}
	libusb_free_device_list( devs, 1 );

	if( sv->udev[USB_DEV_HMD] && AttachInterface( sv, hmd, USB_IF_HMD,        sv->udev[USB_DEV_HMD],        0x81, survive_data_cb, "Mainboard" ) ) { return -6; }
	if( sv->udev[USB_DEV_LIGHTHOUSE] && AttachInterface( sv, hmd, USB_IF_LIGHTHOUSE, sv->udev[USB_DEV_LIGHTHOUSE], 0x81, survive_data_cb, "Lighthouse" ) ) { return -7; }
	if( sv->udev[USB_DEV_WATCHMAN1] && AttachInterface( sv, wm0, USB_IF_WATCHMAN1,  sv->udev[USB_DEV_WATCHMAN1],  0x81, survive_data_cb, "Watchman 1" ) ) { return -8; }
	if( sv->udev[USB_DEV_WATCHMAN2] && AttachInterface( sv, wm1, USB_IF_WATCHMAN2, sv->udev[USB_DEV_WATCHMAN2], 0x81, survive_data_cb, "Watchman 2")) { return -9; }
	if( sv->udev[USB_DEV_TRACKER0] && AttachInterface( sv, tr0, USB_IF_TRACKER0, sv->udev[USB_DEV_TRACKER0], 0x81, survive_data_cb, "Tracker 1")) { return -10; }
	if( sv->udev[USB_DEV_LIGHTHOUSE] && AttachInterface( sv, hmd, USB_IF_LIGHTCAP, sv->udev[USB_DEV_LIGHTHOUSE], 0x82, survive_data_cb, "Lightcap")) { return -12; }

	SV_INFO( "All enumerated devices attached." );

	survive_vive_send_magic(ctx, sv, 1, 0, 0 );

	//libUSB initialized.  Continue.
	return 0;
}

int survive_vive_send_magic(struct SurviveContext * ctx, void * drv, int magic_code, void * data, int datalen )
{
	int r;
	struct SurviveViveData * sv = drv;
	printf( "*CALLING %p %p\n", ctx, sv );

	//XXX TODO: Handle haptics, etc.
	int turnon = magic_code;


	if( turnon )
	{
		//Magic from vl_magic.h, originally copywritten under LGPL. 
		// * Copyright (C) 2013 Fredrik Hultin
		// * Copyright (C) 2013 Jakob Bornecrantz
#if 0
		static uint8_t vive_magic_power_on[] = {
			0x04, 0x78, 0x29, 0x38, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
			0xa8, 0x0d, 0x76, 0x00, 0x40, 0xfc, 0x01, 0x05, 0xfa, 0xec, 0xd1, 0x6d, 0x00,
			0x00, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8, 0x0d, 0x76, 0x00, 0x68, 0xfc,
			0x01, 0x05, 0x2c, 0xb0, 0x2e, 0x65, 0x7a, 0x0d, 0x76, 0x00, 0x68, 0x54, 0x72,
			0x00, 0x18, 0x54, 0x72, 0x00, 0x00, 0x6a, 0x72, 0x00, 0x00, 0x00, 0x00,
		};
#else
		//From actual steam.
		static uint8_t vive_magic_power_on[64] = {   0x04, 0x78, 0x29, 0x38,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,	 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
#endif
		if (sv->udev[USB_DEV_HMD])
		{
			r = update_feature_report( sv->udev[USB_DEV_HMD], 0, vive_magic_power_on, sizeof( vive_magic_power_on ) );
			if( r != sizeof( vive_magic_power_on ) ) return 5;
		}

		if (sv->udev[USB_DEV_LIGHTHOUSE])
		{
			static uint8_t vive_magic_enable_lighthouse[64] = { 0x04 };  //[64] wat?  Why did that fix it?
			r = update_feature_report( sv->udev[USB_DEV_LIGHTHOUSE], 0, vive_magic_enable_lighthouse, sizeof( vive_magic_enable_lighthouse ) );
			if( r != sizeof( vive_magic_enable_lighthouse ) ) return 5;
		}

#if 0
		for( i = 0; i < 256; i++ )
		{
			static uint8_t vive_controller_haptic_pulse[64] = { 0xff, 0x8f, 0xff, 0, 0, 0, 0, 0, 0, 0 };
			r = update_feature_report( sv->udev[USB_DEV_WATCHMAN1], 0, vive_controller_haptic_pulse, sizeof( vive_controller_haptic_pulse ) );
			SV_INFO( "UCR: %d", r );
			if( r != sizeof( vive_controller_haptic_pulse ) ) return 5;
			usleep( 1000 );
		}
#endif
		SV_INFO( "Powered unit on." );
	}
	else
	{

		static uint8_t vive_magic_power_off1[] = {
			0x04, 0x78, 0x29, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
			0x30, 0x05, 0x77, 0x00, 0x30, 0x05, 0x77, 0x00, 0x6c, 0x4d, 0x37, 0x65, 0x40,
			0xf9, 0x33, 0x00, 0x04, 0xf8, 0xa3, 0x04, 0x04, 0x00, 0x00, 0x00, 0x70, 0xb0,
			0x72, 0x00, 0xf4, 0xf7, 0xa3, 0x04, 0x7c, 0xf8, 0x33, 0x00, 0x0c, 0xf8, 0xa3,
			0x04, 0x0a, 0x6e, 0x29, 0x65, 0x24, 0xf9, 0x33, 0x00, 0x00, 0x00, 0x00,
		};

		static uint8_t vive_magic_power_off2[] = {
			0x04, 0x78, 0x29, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
			0x30, 0x05, 0x77, 0x00, 0xe4, 0xf7, 0x33, 0x00, 0xe4, 0xf7, 0x33, 0x00, 0x60,
			0x6e, 0x72, 0x00, 0xb4, 0xf7, 0x33, 0x00, 0x04, 0x00, 0x00, 0x00, 0x70, 0xb0,
			0x72, 0x00, 0x90, 0xf7, 0x33, 0x00, 0x7c, 0xf8, 0x33, 0x00, 0xd0, 0xf7, 0x33,
			0x00, 0x3c, 0x68, 0x29, 0x65, 0x24, 0xf9, 0x33, 0x00, 0x00, 0x00, 0x00,
		};

//		r = update_feature_report( sv->udev[USB_DEV_HMD], 0, vive_magic_power_off1, sizeof( vive_magic_power_off1 ) );
//		SV_INFO( "UCR: %d", r );
//		if( r != sizeof( vive_magic_power_off1 ) ) return 5;

		if (sv->udev[USB_DEV_HMD])
		{
			r = update_feature_report( sv->udev[USB_DEV_HMD], 0, vive_magic_power_off2, sizeof( vive_magic_power_off2 ) );
			SV_INFO( "UCR: %d", r );
			if( r != sizeof( vive_magic_power_off2 ) ) return 5;
		}
	}

}

void survive_vive_usb_close( struct SurviveViveData * sv )
{
	int i;
	for( i = 0; i < MAX_USB_DEVS; i++ )
	{
	    libusb_close( sv->udev[i] );
	}
    libusb_exit(sv->usbctx);	
}

int survive_vive_usb_poll( struct SurviveContext * ctx, void * v )
{
	struct SurviveViveData * sv = v;
	int r = libusb_handle_events( sv->usbctx );
	if( r )
	{
		struct SurviveContext * ctx = sv->ctx;
		SV_ERROR( "Libusb poll failed." );
	}
	return r;
}


int survive_get_config( char ** config, struct SurviveViveData * sv, int devno, int iface, int send_extra_magic )
{
	struct SurviveContext * ctx = sv->ctx;
	int i, ret, count = 0, size = 0;
	uint8_t cfgbuff[64];
	uint8_t compressed_data[8192];
	uint8_t uncompressed_data[65536];
	struct libusb_device_handle * dev = sv->udev[devno];

	if( send_extra_magic )
	{
		uint8_t cfgbuffwide[65];

		memset( cfgbuffwide, 0, sizeof( cfgbuff ) );
		cfgbuffwide[0] = 0x01;
		ret = hid_get_feature_report_timeout( dev, iface, cfgbuffwide, sizeof( cfgbuffwide ) );
		usleep(1000);

		int k;

		//Switch mode to pull config?
		for( k = 0; k < 10; k++ )
		{
			uint8_t cfgbuff_send[64] = { 
				0xff, 0x83, 0x00, 0xb6, 0x5b, 0xb0, 0x78, 0x69,
				0x0f, 0xf8, 0x78, 0x69, 0x0f, 0xa0, 0xf3, 0x18,
				0x00, 0xe8,	0xf2, 0x18, 0x00, 0x27, 0x44, 0x5a,
				0x0f, 0xf8, 0x78, 0x69, 0x0f, 0xf0, 0x77, 0x69,
				0x0f, 0xf0, 0x77, 0x69, 0x0f, 0x50, 0xca, 0x45,
				0x77, 0xa0, 0xf3, 0x18, 0x00, 0xf8, 0x78, 0x69,
				0x0f, 0x00, 0x00, 0xa0, 0x0f, 0xa0, 0x9b, 0x0a,
				0x01, 0x00, 0x00, 0x35, 0x00, 0x34, 0x02, 0x00
			};

			int rk = libusb_control_transfer(dev, LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
			0x09, 0x300 | cfgbuff_send[0], iface, cfgbuff_send, 64, 1000 );
			usleep(1000);
		}

		cfgbuffwide[0] = 0xff;
		ret = hid_get_feature_report_timeout( dev, iface, cfgbuffwide, sizeof( cfgbuffwide ) );
		usleep(1000);
	}

	memset( cfgbuff, 0, sizeof( cfgbuff ) );
	cfgbuff[0] = 0x10;
	if( ( ret = hid_get_feature_report_timeout( dev, iface, cfgbuff, sizeof( cfgbuff ) ) ) < 0 )
	{
		SV_INFO( "Could not get survive config data for device %d:%d", devno, iface );
		return -1;
	}


	cfgbuff[1] = 0xaa;
	cfgbuff[0] = 0x11;
	do
	{
		if( (ret = hid_get_feature_report_timeout(dev, iface, cfgbuff, sizeof( cfgbuff ) ) ) < 0 )
		{
			SV_INFO( "Could not read config data (after first packet) on device %d:%d (count: %d)\n", devno, iface, count );
			return -2;
		}

		size = cfgbuff[1];

		if( !size ) break;

		if( size > 62 )
		{
			SV_INFO( "Too much data (%d) on packet from config for device %d:%d (count: %d)", size, devno, iface, count );
			return -3;
		}

		if( count + size >= sizeof( compressed_data ) )
		{
			SV_INFO( "Configuration length too long %d:%d (count: %d)", devno, iface, count );
			return -4;
		}

        memcpy( &compressed_data[count], cfgbuff + 2, size );
		count += size;
	} while( 1 );

	if( count == 0 )
	{
		SV_INFO( "Empty configuration for %d:%d", devno, iface );
		return -5;
	}

	SV_INFO( "Got config data length %d", count );

	int len = survive_simple_inflate( ctx, compressed_data, count, uncompressed_data, sizeof(uncompressed_data)-1 );
	if( len <= 0 )
	{
		SV_INFO( "Error: data for config descriptor %d:%d is bad.", devno, iface );
		return -5;
	}

	*config = malloc( len + 1 );
	memcpy( *config, uncompressed_data, len );
	return len;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#define POP1  (*(readdata++))
#define POP2  (*(((uint16_t*)((readdata+=2)-2))))
#define POP4  (*(((uint32_t*)((readdata+=4)-4))))

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
		//XXX XXX BIG TODO!!! Actually recal gyro data.
		FLT agm[9] = { readdata[1], readdata[2], readdata[3],
						readdata[4], readdata[5], readdata[6],
						0,0,0 };

		w->ctx->imuproc( w, 3, agm, (time1<<24)|(time2<<16)|readdata[0], 0 );

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
		*readdata = type; //Put 'type' back on stack.
		uint8_t * mptr = readdata + qty-3-1; //-3 for timecode, -1 to 

//#define DEBUG_WATCHMAN
#ifdef DEBUG_WATCHMAN
		printf( "_%s ", w->codename);
		for( i = 0; i < qty; i++ )
		{
			printf( "%02x ", readdata[i] );
		}
		printf("\n");
#endif


		uint32_t mytime = (mptr[3] << 16)|(mptr[2] << 8)|(mptr[1] << 0);

		uint32_t times[20];
		const int nrtime = sizeof(times)/sizeof(uint32_t);
		int timecount = 0;
		int leds;
		int parameters;
		int fault = 0;

		///Handle uint32_tifying (making sure we keep it incrementing)
		uint32_t llt = w->last_lighttime;
		uint32_t imumsb = time1<<24;
		mytime |= imumsb;

		//Compare mytime to llt

		int diff = mytime - llt;
		if( diff < -0x1000000 )
			mytime += 0x1000000;
		else if( diff > 0x100000 )
			mytime -= 0x1000000;

		w->last_lighttime = mytime;

		times[timecount++] = mytime;
#ifdef DEBUG_WATCHMAN
		printf( "_%s Packet Start Time: %d\n", w->codename, mytime );
#endif	

		//First, pull off the times, starting with the current time, then all the delta times going backwards.
		{
			while( mptr - readdata > (timecount>>1) )
			{
				uint32_t arcane_value = 0;
				//ArcanePop (Pop off values from the back, forward, checking if the MSB is set)
				do
				{
					uint8_t ap = *(mptr--);
					arcane_value |= (ap&0x7f);
					if( ap & 0x80 )  break;
					arcane_value <<= 7;
				} while(1);
				times[timecount++] = (mytime -= arcane_value);
#ifdef DEBUG_WATCHMAN
				printf( "_%s Time: %d  newtime: %d\n", w->codename, arcane_value, mytime );
#endif
			}

			leds = timecount>>1;
			//Check that the # of sensors at the beginning match the # of parameters we would expect.
			if( timecount & 1 ) { fault = 1; goto end; }				//Inordinal LED count
			if( leds != mptr - readdata + 1 ) { fault = 2; goto end; }	//LED Count does not line up with parameters
		}


		LightcapElement les[10];
		int lese = 0; //les's end

		//Second, go through all LEDs and extract the lightevent from them. 
		{
			uint8_t marked[nrtime];
			memset( marked, 0, sizeof( marked ) );
			int i, parpl = 0;
			timecount--;
			int timepl = 0;

			//This works, but usually returns the values in reverse end-time order.
			for( i = 0; i < leds; i++ )
			{
				int led = readdata[i];
				int adv = led & 0x07;
				led >>= 3;

				while( marked[timepl] ) timepl++;
				if( timepl > timecount ) { fault = 3; goto end; }         //Ran off max of list.
				uint32_t endtime = times[timepl++];
				int end = timepl + adv;
				if( end > timecount ) { fault = 4; goto end; } //end referencing off list
				if( marked[end] > 0 ) { fault = 5; goto end; } //Already marked trying to be used.
				uint32_t starttime = times[end];
				marked[end] = 1;

				//Insert all lighting things into a sorted list.  This list will be
				//reverse sorted, but that is to minimize operations.  To read it
				//in sorted order simply read it back backwards.
				//Use insertion sort, since we should most of the time, be in order.
				LightcapElement * le = &les[lese++];
				le->sensor_id = led;
				le->type = 0xfe;

				if( (uint32_t)(endtime - starttime) > 65535 ) { fault = 6; goto end; } //Length of pulse dumb.
				le->length = endtime - starttime;
				le->timestamp = starttime;

#ifdef DEBUG_WATCHMAN
				printf( "_%s Event: %d %d %d-%d\n", w->codename, led, le->length, endtime, starttime );
#endif
				int swap = lese-2;
				while( swap >= 0 && les[swap].timestamp < les[swap+1].timestamp )
				{
					LightcapElement l;
					memcpy( &l, &les[swap], sizeof( l ) );
					memcpy( &les[swap], &les[swap+1], sizeof( l ) );
					memcpy( &les[swap+1], &l, sizeof( l ) );
					swap--;
				}
			}
		}

		int i;
		for( i = lese-1; i >= 0; i-- )
		{
			//printf( "%d: %d [%d]\n", les[i].sensor_id, les[i].length, les[i].timestamp );
			handle_lightcap( w, &les[i] );
		}

		return;
	end:
		{
			SurviveContext * ctx = w->ctx;
			SV_INFO( "Light decoding fault: %d\n", fault );
		}
	}
}


void survive_data_cb( SurviveUSBInterface * si )
{
	int size = si->actual_len;
	SurviveContext * ctx = si->ctx;
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
	SurviveObject * obj = si->assoc_obj;
	uint8_t * readdata = si->buffer;

	int id = POP1;
//	printf( "%16s Size: %2d ID: %d / %d\n", si->hname, size, id, iface );


	switch( si->which_interface_am_i )
	{
	case USB_IF_HMD:
	{
		struct SurviveObject * headset = obj;
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
			int16_t * acceldata = (int16_t*)readdata;
			readdata += 12;
			uint32_t timecode = POP4;
			uint8_t code = POP1;
			//printf( "%d ", code );
			int8_t cd = code - obj->oldcode;

			if( cd > 0 )
			{
				obj->oldcode = code;

				//XXX XXX BIG TODO!!! Actually recal gyro data.
				FLT agm[9] = { acceldata[0], acceldata[1], acceldata[2],
								acceldata[3], acceldata[4], acceldata[5],
								0,0,0 };

				ctx->imuproc( obj, 3, agm, timecode, code );
			}
		}

		//DONE OK.
		break;
	}
	case USB_IF_WATCHMAN1:
	case USB_IF_WATCHMAN2:
	{
		SurviveObject * w = obj;
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
	case USB_IF_TRACKER0:
	{
		SurviveObject * w = obj;
		if( id == 32 )
		{
			// TODO: Looks like this will need to be handle_tracker, since
			// it appears the interface is sufficiently different.
			// More work needd to reverse engineer it.
			handle_watchman( w, readdata);
		}
		else
		{
			SV_INFO( "Unknown tracker code %d\n", id );
		}
		break;
	}
	case USB_IF_LIGHTCAP:
	{
		//Done!
		int i;
		for( i = 0; i < 7; i++ )
		{
			handle_lightcap( obj, (LightcapElement*)&readdata[i*8] );
		}
		break;
	}
	}
}










///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////



static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
 if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
    strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}


static int ParsePoints( SurviveContext * ctx, SurviveObject * so, char * ct0conf, FLT ** floats_out, jsmntok_t * t, int i )
{
	int k;
	int pts = t[i+1].size;
	jsmntok_t * tk;

	so->nr_locations = 0;
	*floats_out = malloc( sizeof( **floats_out ) * 32 * 3 );

	for( k = 0; k < pts; k++ )
	{
		tk = &t[i+2+k*4];

		FLT vals[3];
		int m;
		for( m = 0; m < 3; m++ )
		{
			char ctt[128];

			tk++;
			int elemlen = tk->end - tk->start;

			if( tk->type != 4 || elemlen > sizeof( ctt )-1 )
			{
				SV_ERROR( "Parse error in JSON\n" );
				return 1;
			}

			memcpy( ctt, ct0conf + tk->start, elemlen );
			ctt[elemlen] = 0;
			FLT f = atof( ctt );
			int id = so->nr_locations*3+m;
			(*floats_out)[id] = f;
		}
		so->nr_locations++;
	}
	return 0;
}

static int LoadConfig( SurviveViveData * sv, SurviveObject * so, int devno, int iface, int extra_magic )
{
	SurviveContext * ctx = sv->ctx;
	char * ct0conf = 0;
	int len = survive_get_config( &ct0conf, sv, devno, iface, extra_magic );

#if 0
	char fname[100];
	sprintf( fname, "%s_config.json", so->codename );
	FILE * f = fopen( fname, "w" );
	fwrite( ct0conf, strlen(ct0conf), 1, f );
	fclose( f );
#endif

	if( len > 0 )
	{

		//From JSMN example.
		jsmn_parser p;
		jsmntok_t t[4096];
		jsmn_init(&p);
		int i;
		int r = jsmn_parse(&p, ct0conf, len, t, sizeof(t)/sizeof(t[0]));	
		if (r < 0) {
			SV_INFO("Failed to parse JSON in HMD configuration: %d\n", r);
			return -1;
		}
		if (r < 1 || t[0].type != JSMN_OBJECT) {
			SV_INFO("Object expected in HMD configuration\n");
			return -2;
		}

		for (i = 1; i < r; i++) {
			jsmntok_t * tk = &t[i];

			char ctxo[100];
			int ilen = tk->end - tk->start;
			if( ilen > 99 ) ilen = 99;
			memcpy(ctxo, ct0conf + tk->start, ilen);
			ctxo[ilen] = 0;

//				printf( "%d / %d / %d / %d %s %d\n", tk->type, tk->start, tk->end, tk->size, ctxo, jsoneq(ct0conf, &t[i], "modelPoints") );
//				printf( "%.*s\n", ilen, ct0conf + tk->start );

			if (jsoneq(ct0conf, tk, "modelPoints") == 0) {
				if( ParsePoints( ctx, so, ct0conf, &so->sensor_locations, t, i  ) )
				{
					break;
				}
			}
			if (jsoneq(ct0conf, tk, "modelNormals") == 0) {
				if( ParsePoints( ctx, so, ct0conf, &so->sensor_normals, t, i  ) )
				{
					break;
				}
			}
		}
	}
	else
	{
		//TODO: Cleanup any remaining USB stuff.
		return 1;
	}

	char fname[20];
	mkdir( "calinfo", 0755 );

	sprintf( fname, "calinfo/%s_points.csv", so->codename );
	FILE * f = fopen( fname, "w" );
	int j;
	for( j = 0; j < so->nr_locations; j++ )
	{
		fprintf( f, "%f %f %f\n", so->sensor_locations[j*3+0], so->sensor_locations[j*3+1], so->sensor_locations[j*3+2] );
	}
	fclose( f );

	sprintf( fname, "calinfo/%s_normals.csv", so->codename );
	f = fopen( fname, "w" );
	for( j = 0; j < so->nr_locations; j++ )
	{
		fprintf( f, "%f %f %f\n", so->sensor_normals[j*3+0], so->sensor_normals[j*3+1], so->sensor_normals[j*3+2] );
	}
	fclose( f );

	return 0;
}


int survive_vive_close( SurviveContext * ctx, void * driver )
{
	SurviveViveData * sv = driver;
 
	survive_vive_usb_close( sv );
}


int DriverRegHTCVive( SurviveContext * ctx )
{
	int i, r;
	SurviveObject * hmd = calloc( 1, sizeof( SurviveObject ) );
	SurviveObject * wm0 = calloc( 1, sizeof( SurviveObject ) );
	SurviveObject * wm1 = calloc( 1, sizeof( SurviveObject ) );
	SurviveObject * tr0 = calloc( 1, sizeof( SurviveObject ) );
	SurviveViveData * sv = calloc( 1, sizeof( SurviveViveData ) );

	sv->ctx = ctx;

	hmd->ctx = ctx;
	memcpy( hmd->codename, "HMD", 4 );
	memcpy( hmd->drivername, "HTC", 4 );
	wm0->ctx = ctx;
	memcpy( wm0->codename, "WM0", 4 );
	memcpy( wm0->drivername, "HTC", 4 );
	wm1->ctx = ctx;
	memcpy( wm1->codename, "WM1", 4 );
	memcpy( wm1->drivername, "HTC", 4 );
	tr0->ctx = ctx;
	memcpy( tr0->codename, "TR0", 4 );
	memcpy( tr0->drivername, "HTC", 4 );

	//USB must happen last.
	if( r = survive_usb_init( sv, hmd, wm0, wm1, tr0) )
	{
		//TODO: Cleanup any libUSB stuff sitting around.
		goto fail_gracefully;
	}

	//Next, pull out the config stuff.
	if( sv->udev[USB_DEV_HMD]       && LoadConfig( sv, hmd, 1, 0, 0 )) { SV_INFO( "HMD config issue." ); }
	if( sv->udev[USB_DEV_WATCHMAN1] && LoadConfig( sv, wm0, 2, 0, 1 )) { SV_INFO( "Watchman 0 config issue." ); }
	if( sv->udev[USB_DEV_WATCHMAN2] && LoadConfig( sv, wm1, 3, 0, 1 )) { SV_INFO( "Watchman 1 config issue." ); }
	if( sv->udev[USB_DEV_TRACKER0]  && LoadConfig( sv, tr0, 4, 0, 1 )) { SV_INFO( "Tracker 0 config issue." ); }

	hmd->timebase_hz = wm0->timebase_hz = wm1->timebase_hz = 48000000;
	hmd->pulsedist_max_ticks = wm0->pulsedist_max_ticks = wm1->pulsedist_max_ticks = 500000;
	hmd->pulselength_min_sync = wm0->pulselength_min_sync = wm1->pulselength_min_sync = 2200;
	hmd->pulse_in_clear_time = wm0->pulse_in_clear_time = wm1->pulse_in_clear_time = 35000;
	hmd->pulse_max_for_sweep = wm0->pulse_max_for_sweep = wm1->pulse_max_for_sweep = 1800;

	hmd->pulse_synctime_offset = wm0->pulse_synctime_offset = wm1->pulse_synctime_offset = 20000;
	hmd->pulse_synctime_slack = wm0->pulse_synctime_slack = wm1->pulse_synctime_slack = 5000;

	hmd->timecenter_ticks = hmd->timebase_hz / 240;
	wm0->timecenter_ticks = wm0->timebase_hz / 240;
	wm1->timecenter_ticks = wm1->timebase_hz / 240;
/*
	int i;
	int locs = hmd->nr_locations;
	printf( "Locs: %d\n", locs );
	if (hmd->sensor_locations )
	{
		printf( "POSITIONS:\n" );
		for( i = 0; i < locs*3; i+=3 )
		{
			printf( "%f %f %f\n", hmd->sensor_locations[i+0], hmd->sensor_locations[i+1], hmd->sensor_locations[i+2] );
		}
	}
	if( hmd->sensor_normals )
	{
		printf( "NORMALS:\n" );
		for( i = 0; i < locs*3; i+=3 )
		{
			printf( "%f %f %f\n", hmd->sensor_normals[i+0], hmd->sensor_normals[i+1], hmd->sensor_normals[i+2] );
		}
	}
*/

	//Add the drivers.
	if( sv->udev[USB_DEV_HMD]       ) { survive_add_object( ctx, hmd ); }
	if( sv->udev[USB_DEV_WATCHMAN1] ) { survive_add_object( ctx, wm0 ); }
	if( sv->udev[USB_DEV_WATCHMAN2] ) { survive_add_object( ctx, wm1 ); }
	if( sv->udev[USB_DEV_TRACKER0]  ) { survive_add_object( ctx, tr0 ); }

	survive_add_driver( ctx, sv, survive_vive_usb_poll, survive_vive_close, survive_vive_send_magic );

	return 0;
fail_gracefully:
	free( hmd );
	free( wm0 );
	free( wm1 );
	free( tr0 );
	survive_vive_usb_close( sv );
	free( sv );
	return -1;
}

REGISTER_LINKTIME( DriverRegHTCVive );

