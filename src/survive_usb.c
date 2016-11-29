#include "survive_internal.h"
#include <libusb-1.0/libusb.h>
#include <stdio.h>

const short vidpids[] = {
	0x0bb4, 0x2c87, 0, //The main HTC HMD device
	0x28de, 0x2000, 0, //Valve lighthouse
	0x28de, 0x2101, 0, //Valve Watchman
	0x28de, 0x2101, 1, //Valve Watchman
}; //length MAX_USB_INTERFACES*2

const char * devnames[] = {
	"HMD",
	"Lighthouse",
	"Watchman 1",
	"Watchman 2",
}; //length MAX_USB_INTERFACES


static void handle_transfer(struct libusb_transfer* transfer) {
    struct SurviveUSBInterface * iface = transfer->user_data;

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
		SV_ERROR( "Error resubmitting transfer for %s\n", iface->hname );
		SV_KILL();
	}
}



static int AttachInterface( struct SurviveContext * t, int which_interface_am_i, libusb_device_handle * devh, int endpoint, usb_callback cb, const char * hname )
{
	struct SurviveUSBInterface * iface = &t->uiface[which_interface_am_i];
	iface->ctx = t;
	iface->which_interface_am_i = which_interface_am_i;
	iface->hname = hname;
	struct libusb_transfer * tx = iface->transfer = libusb_alloc_transfer(0);
	iface->cb = cb;
	printf( "%p %d %p %p\n", iface, which_interface_am_i, tx, devh );

	if (!iface->transfer)
	{
		SV_ERROR( "Error: failed on libusb_alloc_transfer for %s\n", hname );
		return 4;
	}

	libusb_fill_interrupt_transfer( tx, devh, endpoint, iface->buffer, INTBUFFSIZE, handle_transfer, iface, 0);

	int rc = libusb_submit_transfer( tx );
    if( rc )
	{
		SV_ERROR( "Error: Could not submit transfer for %s (Code %d)\n", hname, rc );
		return 6;
	}

	return 0;
}


void debug_cb( struct SurviveUSBInterface * si )
{
	int i;
	int len = si->actual_len;
	printf( "%16s: %d: ", si->hname, len );
	for( i = 0; i < len; i++ )
	{
		printf( "%02x ", si->buffer[i] );
	}
	printf( "\n" );
}

int survive_usb_init( struct SurviveContext * t )
{
	int r = libusb_init( &t->usbctx );
	if( r )
	{
		SV_ERROR( "libusb fault %d\n", r );
		return r;
	}

	int i;
	libusb_device** devs;
	int ret = libusb_get_device_list(t->usbctx, &devs);

	if( ret < 0 )
	{
		SV_ERROR( "Couldn't get list of USB devices %d\n", ret );
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

		struct libusb_config_descriptor *conf;
		ret = libusb_get_config_descriptor(d, 0, &conf);
		if( ret )
			continue;

		ret = libusb_open(d, &t->udev[i]);

		if( !t->udev[i] || ret )
		{
			SV_ERROR( "Error: cannot open device \"%s\" with vid/pid %04x:%04x\n", devnames[i], vid, pid );
			return -5;
		}

		libusb_set_auto_detach_kernel_driver( t->udev[i], 1 );

        for (int j = 0; j < conf->bNumInterfaces; j++ )
		{
			if( libusb_claim_interface(t->udev[i], j) )
			{
				SV_ERROR( "Could not claim interface %d of %s\n", j, devnames[i] );
				return -9;
			}
		}

		SV_INFO( "Successfully enumerated %s (%d, %d)\n", devnames[i], did, conf->bNumInterfaces );
	}
	libusb_free_device_list( devs, 1 );

	if( AttachInterface( t, USB_IF_HMD, 		t->udev[USB_DEV_HMD], 0x81, debug_cb, "Mainboard" ) ) { return -6; }
	if( AttachInterface( t, USB_IF_LIGHTHOUSE,	t->udev[USB_DEV_LIGHTHOUSE], 0x81, debug_cb, "Lighthouse" ) ) { return -6; }
	if( AttachInterface( t, USB_IF_WATCHMAN1,	t->udev[USB_DEV_WATCHMAN1], 0x81, debug_cb, "Watchman 1" ) ) { return -6; }
	if( AttachInterface( t, USB_IF_WATCHMAN2,	t->udev[USB_DEV_WATCHMAN2], 0x81, debug_cb, "Watchman 2" ) ) { return -6; }
	if( AttachInterface( t, USB_IF_LIGHTCAP,	t->udev[USB_DEV_LIGHTHOUSE], 0x82, debug_cb, "Lightcap" ) ) { return -6; }

	SV_INFO( "All devices attached.\n" );


	//libUSB initialized.  Continue.
	return 0;
}


void survive_usb_close( struct SurviveContext * t )
{
	int i;
	for( i = 0; i < MAX_USB_DEVS; i++ )
	{
	    libusb_close( t->udev[i] );
	}
    libusb_exit(t->usbctx);	
}

int survive_usb_poll( struct SurviveContext * ctx )
{
	int r = libusb_handle_events( ctx->usbctx );
	if( r )
	{
		SV_ERROR( "Libusb poll failed.\n" );
	}
	return r;
}

