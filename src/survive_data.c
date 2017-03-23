//<>< (C) 2016 C. N. Lohr, MOSTLY Under MIT/x11 License.
//

#include "survive_internal.h"
#include <stdint.h>
#include <string.h>

typedef struct
{
	unsigned int sweep_time[SENSORS_PER_OBJECT];
	unsigned int sweep_len[SENSORS_PER_OBJECT];
} lightcaps_sweep_data;
typedef struct
{
	int recent_sync_time;
	int activeLighthouse;
	int activeSweepStartTime;
	int activeAcode;

	int lh_pulse_len[NUM_LIGHTHOUSES];
	int lh_start_time[NUM_LIGHTHOUSES];
	int current_lh; // used knowing which sync pulse we're looking at.

} lightcap2_global_data;

typedef struct
{
	lightcaps_sweep_data sweep;
	lightcap2_global_data global;
} lightcap2_data;


//static lightcap2_global_data lcgd = { 0 };

int handle_lightcap2_getAcodeFromSyncPulse(int pulseLen)
{
	if (pulseLen < 3125) return 0;
	if (pulseLen < 3625) return 1;
	if (pulseLen < 4125) return 2;
	if (pulseLen < 4625) return 3;
	if (pulseLen < 5125) return 4;
	if (pulseLen < 5625) return 5;
	if (pulseLen < 6125) return 6;
	return 7;
}
void handle_lightcap2_process_sweep_data(SurviveObject *so)
{
	lightcap2_data *lcd = so->disambiguator_data;

	// look at all of the sensors we found, and process the ones that were hit.
	// TODO: find the sensor(s) with the longest pulse length, and assume 
	// those are the "highest quality".  Then, reject any pulses that are sufficiently
	// different from those values, assuming that they are reflections.
	{
		unsigned int longest_pulse = 0;
		unsigned int timestamp_of_longest_pulse = 0;
		for (int i = 0; i < SENSORS_PER_OBJECT; i++)
		{
			if (lcd->sweep.sweep_len[i] > longest_pulse)
			{
				longest_pulse = lcd->sweep.sweep_len[i];
				timestamp_of_longest_pulse = lcd->sweep.sweep_time[i];
			}
		}

		for (int i = 0; i < SENSORS_PER_OBJECT; i++)
		{
			if (lcd->sweep.sweep_len[i] != 0) // if the sensor was hit, process it
			{
				int offset_from = lcd->sweep.sweep_time[i] - lcd->global.activeSweepStartTime + lcd->sweep.sweep_len[i] / 2;

				if (offset_from < 380000 && offset_from > 70000)
				{
					if (longest_pulse *10 / 8 < lcd->sweep.sweep_len[i]) 
					{
						so->ctx->lightproc(so, i, lcd->global.activeAcode, offset_from, lcd->sweep.sweep_time[i], lcd->sweep.sweep_len[i], lcd->global.activeLighthouse);
					}
				}
			}
		}
	}
	// clear out sweep data (could probably limit this to only after a "first" sync.  
	// this is slightly more robust, so doing it here for now.
	memset(&(((lightcap2_data*)so->disambiguator_data)->sweep), 0, sizeof(lightcaps_sweep_data));
}
void handle_lightcap2_sync(SurviveObject * so, LightcapElement * le )
{
	//fprintf(stderr, "%6.6d %4.4d \n", le->timestamp - so->recent_sync_time, le->length);
	lightcap2_data *lcd = so->disambiguator_data;

	//static unsigned int recent_sync_time = 0;
	//static unsigned int recent_sync_count = -1;
	//static unsigned int activeSweepStartTime;


	// Process any sweep data we have
	handle_lightcap2_process_sweep_data(so);

	int time_since_last_sync = (le->timestamp - lcd->global.recent_sync_time);

	//fprintf(stderr, "            %2d %8d %d\n", le->sensor_id, time_since_last_sync, le->length);
	// need to store up sync pulses, so we can take the earliest starting time for all sensors.
	if (time_since_last_sync < 2400)
	{
		lcd->global.recent_sync_time = le->timestamp;
		// it's the same sync pulse;
		so->sync_set_number = 1;
		so->recent_sync_time = le->timestamp;

		lcd->global.lh_pulse_len[lcd->global.current_lh] = le->length;
		lcd->global.lh_start_time[lcd->global.current_lh] = le->timestamp;

		int acode = handle_lightcap2_getAcodeFromSyncPulse(le->length);
		if (!(acode >> 2 & 1)) // if the skip bit is not set
		{
			lcd->global.activeLighthouse = lcd->global.current_lh;
			lcd->global.activeSweepStartTime = le->timestamp;
			lcd->global.activeAcode = acode;
		}
		else
		{
			lcd->global.activeLighthouse = -1;
			lcd->global.activeSweepStartTime = 0;
			lcd->global.activeAcode = 0;
		}
	}
	else if (time_since_last_sync < 24000)
	{
		lcd->global.recent_sync_time = le->timestamp;
		// I do believe we are lighthouse B		
		lcd->global.current_lh = 1;
		lcd->global.lh_pulse_len[lcd->global.current_lh] = le->length;
		lcd->global.lh_start_time[lcd->global.current_lh] = le->timestamp;

		int acode = handle_lightcap2_getAcodeFromSyncPulse(le->length);

		if (!(acode >> 2 & 1)) // if the skip bit is not set
		{
			if (lcd->global.activeLighthouse != -1)
			{
				// hmm, it appears we got two non-skip pulses at the same time.  That should never happen
				fprintf(stderr, "WARNING: Two non-skip pulses received on the same cycle!\n");
			}
			lcd->global.activeLighthouse = 1;
			lcd->global.activeSweepStartTime = le->timestamp;
			lcd->global.activeAcode = acode;
		}

	}
	else if (time_since_last_sync > 370000)
	{
		// looks like this is the first sync pulse.  Cool!

		// first, send out the sync pulse data for the last round (for OOTX decoding
		{
			if (lcd->global.lh_pulse_len[0] != 0)
			{
				so->ctx->lightproc(
					so,
					-1,
					handle_lightcap2_getAcodeFromSyncPulse(lcd->global.lh_pulse_len[0]),
					lcd->global.lh_pulse_len[0],
					lcd->global.lh_start_time[0],
					0,
					0);
			}
			if (lcd->global.lh_pulse_len[1] != 0)
			{
				so->ctx->lightproc(
					so,
					-2,
					handle_lightcap2_getAcodeFromSyncPulse(lcd->global.lh_pulse_len[1]),
					lcd->global.lh_pulse_len[1],
					lcd->global.lh_start_time[1],
					0,
					1);
			}
		}

		// initialize here.
		memset(&lcd->global, 0, sizeof(lcd->global));
		lcd->global.activeLighthouse = -1; 



		lcd->global.recent_sync_time = le->timestamp;
		// I do believe we are lighthouse A		
		lcd->global.current_lh = 0;
		lcd->global.lh_pulse_len[lcd->global.current_lh] = le->length;
		lcd->global.lh_start_time[lcd->global.current_lh] = le->timestamp;

		int acode = handle_lightcap2_getAcodeFromSyncPulse(le->length);

		if (!(acode >> 2 & 1)) // if the skip bit is not set
		{
			lcd->global.activeLighthouse = 0;
			lcd->global.activeSweepStartTime = le->timestamp;
			lcd->global.activeAcode = acode;
		}
	}
}

void handle_lightcap2_sweep(SurviveObject * so, LightcapElement * le )
{
	lightcap2_data *lcd = so->disambiguator_data;

	// If we see multiple "hits" on the sweep for a given sensor,
	// assume that the longest (i.e. strongest signal) is most likely 
	// the non-reflected signal. 

	if (le->length < 80)
	{
		// this is a low-quality read.  Better to throw it out than to use it.
		//fprintf(stderr, "%2d %d\n", le->sensor_id, le->length);
		return;
	}
	fprintf(stderr, "%2d %d\n", le->sensor_id, le->length);

	if (lcd->sweep.sweep_len[le->sensor_id] < le->length)
	{
		lcd->sweep.sweep_len[le->sensor_id] = le->length;
		lcd->sweep.sweep_time[le->sensor_id] = le->timestamp;
	}
}

void handle_lightcap2( SurviveObject * so, LightcapElement * le )
{
	SurviveContext * ctx = so->ctx;

	if (so->disambiguator_data == NULL)
	{
		so->disambiguator_data = malloc(sizeof(lightcap2_data));
		memset(so->disambiguator_data, 0, sizeof(lightcap2_data));
	}

	if( le->sensor_id > SENSORS_PER_OBJECT )
	{
		return;
	}

	if (le->length > 6750)
	{
		// Should never get a reading so high.  Odd.
		return;
	}
	if (le->length >= 2750)
	{
		// Looks like a sync pulse, process it!
		handle_lightcap2_sync(so, le);
		return;
	}

	// must be a sweep pulse, process it!
	handle_lightcap2_sweep(so, le);

}


//This is the disambiguator function, for taking light timing and figuring out place-in-sweep for a given photodiode.
void handle_lightcap( SurviveObject * so, LightcapElement * le )
{
	handle_lightcap2(so,le);
	return;

	SurviveContext * ctx = so->ctx;
	//int32_t deltat = (uint32_t)le->timestamp - (uint32_t)so->last_master_time;

	//if( so->codename[0] != 'H' )


	if( le->sensor_id > SENSORS_PER_OBJECT )
	{
		return;
	}

	so->tsl = le->timestamp;
	if( le->length < 20 ) return;  ///Assuming 20 is an okay value for here.

	//The sync pulse finder is taking Charles's old disambiguator code and mixing it with a more linear
	//version of Julian Picht's disambiguator, available in 488c5e9.  Removed afterwards into this
	//unified driver.
	int ssn = so->sync_set_number; //lighthouse number
	if( ssn < 0 ) ssn = 0;
	int last_sync_time  =  so->last_sync_time  [ssn];
	int last_sync_length = so->last_sync_length[ssn];
	int32_t delta = le->timestamp - last_sync_time;  //Handle time wrapping (be sure to be int32)

	if( delta < -so->pulsedist_max_ticks || delta > so->pulsedist_max_ticks )
	{
		//Reset pulse, etc.
		so->sync_set_number = -1;
		delta = so->pulsedist_max_ticks;
//		return; //if we don't know what lighthouse this is we don't care to do much else
	}


	if( le->length > so->pulselength_min_sync ) //Pulse longer indicates a sync pulse.
	{
		int is_new_pulse = delta > so->pulselength_min_sync /*1500*/ + last_sync_length;

		//printf("m sync %d %d %d %d\n", le->sensor_id, so->last_sync_time[ssn], le->timestamp, delta);

		so->did_handle_ootx = 0;

		if( is_new_pulse )
		{
			int is_master_sync_pulse = delta > so->pulse_in_clear_time /*40000*/; 

			if( is_master_sync_pulse )
			{
				ssn = so->sync_set_number = 0;
				so->last_sync_time[ssn] = le->timestamp;
				so->last_sync_length[ssn] = le->length;
			}
			else if( so->sync_set_number == -1 )
			{
				//Do nothing.
			}
			else
			{
				ssn = ++so->sync_set_number;
				if( so->sync_set_number >= NUM_LIGHTHOUSES )
				{
					SV_INFO( "Warning.  Received an extra, unassociated sync pulse." );
					ssn = so->sync_set_number = -1;
				}
				else
				{
					so->last_sync_time[ssn] = le->timestamp;
					so->last_sync_length[ssn] = le->length;
				}
			}
		}
		else
		{
			//Find the longest pulse.
			if( le->length > last_sync_length )
			{
				if( so->last_sync_time[ssn] > le->timestamp )
				{
					so->last_sync_time[ssn] = le->timestamp;
					so->last_sync_length[ssn] = le->length;
				}
			}
		}
	}



	//See if this is a valid actual pulse.
	else if( le->length < so->pulse_max_for_sweep && delta > so->pulse_in_clear_time && ssn >= 0 )
	{
		int32_t dl = so->last_sync_time[0];
		int32_t tpco = so->last_sync_length[0];


#if NUM_LIGHTHOUSES != 2
		#error You are going to have to fix the code around here to allow for something other than two base stations.
#endif

		//Adding length 
		//Long pulse-code from IR flood.
		//Make sure it fits nicely into a divisible-by-500 time.

		int32_t main_divisor = so->timebase_hz / 384000; //125 @ 48 MHz.

		int32_t acode_array[2] =
			{
				(so->last_sync_length[0]+main_divisor+50)/(main_divisor*2),  //+50 adds a small offset and seems to help always get it right. 
				(so->last_sync_length[1]+main_divisor+50)/(main_divisor*2),	//Check the +50 in the future to see how well this works on a variety of hardware.
			};

		//XXX: TODO: Capture error count here.
		if( acode_array[0] & 1 ) return;
		if( acode_array[1] & 1 ) return;

		acode_array[0] = (acode_array[0]>>1) - 6;
		acode_array[1] = (acode_array[1]>>1) - 6;


		int acode = acode_array[0];

		if( !so->did_handle_ootx )
		{
			int32_t delta1 = so->last_sync_time[0] - so->recent_sync_time;
			int32_t delta2 = so->last_sync_time[1] - so->last_sync_time[0];

			ctx->lightproc( so, -1, acode_array[0], delta1, so->last_sync_time[0], so->last_sync_length[0], 0 );
			ctx->lightproc( so, -2, acode_array[1], delta2, so->last_sync_time[1], so->last_sync_length[1], 1 );

			so->recent_sync_time = so->last_sync_time[1];

			//Throw out everything if our sync pulses look like they're bad.

			int32_t center_1 = so->timecenter_ticks*2 - so->pulse_synctime_offset;
			int32_t center_2 = so->pulse_synctime_offset;
			int32_t slack = so->pulse_synctime_slack;

			if( delta1 < center_1 - slack || delta1 > center_1 + slack )
			{
				//XXX: TODO: Count faults.
				so->sync_set_number = -1;
				return;
			}

			if( delta2 < center_2 - slack || delta2 > center_2 + slack )
			{
				//XXX: TODO: Count faults.
				so->sync_set_number = -1;
				return;
			}

			so->did_handle_ootx = 1;
		}


		if (acode > 3) {
			if( ssn == 0 )
			{
				//SV_INFO( "Warning: got a slave marker but only got a master sync." );
				//This happens too frequently.  Consider further examination.
			}
			dl = so->last_sync_time[1];
			tpco = so->last_sync_length[1];
		}

		int32_t offset_from = le->timestamp - dl + le->length/2;

		//Make sure pulse is in valid window
		if( offset_from < 380000 && offset_from > 70000 )
		{
			ctx->lightproc( so, le->sensor_id, acode, offset_from, le->timestamp, le->length, so->sync_set_number );
		}
	}
	else
	{
		//printf( "FAIL %d   %d - %d = %d\n", le->length, so->last_photo_time, le->timestamp, so->last_photo_time - le->timestamp );
		//Runt pulse, or no sync pulses available.
	}
}


