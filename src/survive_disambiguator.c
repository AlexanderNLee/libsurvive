#include "survive.h"

#include "survive_playback.h"
#include <assert.h>
#include <os_generic.h>
#include <stdio.h>

//#define LOG_LIGHTDATA

uint8_t survive_map_sensor_id(SurviveObject *so, uint8_t reported_id) {
	if (so->channel_map) {
		assert(reported_id < 32);
		int ole = reported_id;
		uint8_t mapped_id = so->channel_map[ole];
		if (mapped_id >= so->sensor_ct) {
			SurviveContext *ctx = so->ctx;
			SV_WARN("Invalid sensor %d detected hit (%d)", mapped_id, ole);
			return -1;
		}
		return mapped_id;
	}
	return reported_id;
}

void handle_lightcap(SurviveObject *so, const LightcapElement *_le) {
	LightcapElement le = *_le;
	survive_recording_lightcap(so, &le);
#ifdef LOG_LIGHTDATA
	static FILE *flog;
	static double start = 0;
	if (!flog) {
		flog = fopen("lightcap.txt", "wb");
		start = OGGetAbsoluteTime();
	}
	fprintf(flog, "%.6f %2d %4d %9d\n", OGGetAbsoluteTime() - start, le->sensor_id, le->length, le->timestamp);
#endif
	le.sensor_id = survive_map_sensor_id(so, le.sensor_id);
	if (le.sensor_id == (uint8_t)-1) {
		return;
	}
	so->ctx->lightcapproc(so, &le);
}
