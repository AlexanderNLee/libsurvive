// All MIT/x11 Licensed Code in this file may be relicensed freely under the GPL
// or LGPL licenses.

#include <stdio.h>
#include <stdlib.h>
#include <survive.h>

#include <string.h>
#include <sys/time.h>

#include "survive_config.h"
#include "survive_default_devices.h"

#include "os_generic.h"

struct SurvivePlaybackData {
	SurviveContext *ctx;
	const char *playback_dir;
	FILE *playback_file;
	int lineno;

	FLT time_factor;
	double next_time_us;
};
typedef struct SurvivePlaybackData SurvivePlaybackData;

double timestamp_in_us() {
	static double start_time_us = 0;
	if (start_time_us == 0.)
		start_time_us = OGGetAbsoluteTime();
	return OGGetAbsoluteTime() - start_time_us;
}

static int parse_and_run_imu(const char *line, SurvivePlaybackData *driver) {
	char dev[10];
	int timecode = 0;
	FLT accelgyro[6];
	int mask;
	int id;

	int rr =
		sscanf(line, "I %s %d %d " FLT_format " " FLT_format " " FLT_format
					 " " FLT_format " " FLT_format " " FLT_format "%d",
			   dev, &mask, &timecode, &accelgyro[0], &accelgyro[1],
			   &accelgyro[2], &accelgyro[3], &accelgyro[4], &accelgyro[5], &id);

	if (rr != 10) {
		fprintf(stderr, "Warning:  On line %d, only %d values read: '%s'\n",
				driver->lineno, rr, line);
		return -1;
	}

	SurviveObject *so = survive_get_so_by_name(driver->ctx, dev);
	if (!so) {
		fprintf(stderr, "Could not find device named %s from lineno %d\n", dev,
				driver->lineno);
		return -1;
	}

	driver->ctx->imuproc(so, mask, accelgyro, timecode, id);
	return 0;
}

static int parse_and_run_lightcode(const char *line,
								   SurvivePlaybackData *driver) {
	char lhn[10];
	char axn[10];
	char dev[10];
	uint32_t timecode = 0;
	int sensor = 0;
	int acode = 0;
	int timeinsweep = 0;
	uint32_t pulselength = 0;
	uint32_t lh = 0;

	int rr =
		sscanf(line, "%8s %8s %8s %u %d %d %d %u %u\n", lhn, axn, dev,
			   &timecode, &sensor, &acode, &timeinsweep, &pulselength, &lh);

	if (rr != 9) {
		fprintf(stderr, "Warning:  On line %d, only %d values read: '%s'\n",
				driver->lineno, rr, line);
		return -1;
	}

	SurviveObject *so = survive_get_so_by_name(driver->ctx, dev);
	if (!so) {
		fprintf(stderr, "Could not find device named %s from lineno %d\n", dev,
				driver->lineno);
		return -1;
	}

	driver->ctx->lightproc(so, sensor, acode, timeinsweep, timecode,
						   pulselength, lh);
	return 0;
}

static int playback_poll(struct SurviveContext *ctx, void *_driver) {
	SurvivePlaybackData *driver = _driver;
	FILE *f = driver->playback_file;

	if (f && !feof(f) && !ferror(f)) {
		int i;
		driver->lineno++;
		char *line;

		if (driver->next_time_us == 0) {
			char *buffer;
			size_t n = 0;
			ssize_t r = getdelim(&line, &n, ' ', f);
			if (r <= 0)
				return 0;

			if (sscanf(line, "%lf", &driver->next_time_us) != 1) {
				free(line);
				return 0;
			}
			free(line);
			line = 0;
		}

		if (driver->next_time_us * driver->time_factor > timestamp_in_us())
			return 0;
		driver->next_time_us = 0;

		char *buffer;
		size_t n = 0;
		ssize_t r = getline(&line, &n, f);
		if (r <= 0)
			return 0;

		if ((line[0] != 'R' && line[0] != 'L' && line[0] != 'I') ||
			line[1] != ' ')
			return 0;

		switch (line[0]) {
		case 'L':
		case 'R':
			parse_and_run_lightcode(line, driver);
			break;
		case 'I':
			parse_and_run_imu(line, driver);
			break;
		}

		free(line);
	} else {
		if (f) {
			fclose(driver->playback_file);
		}
		driver->playback_file = 0;
		return -1;
	}

	return 0;
}

static int playback_close(struct SurviveContext *ctx, void *_driver) {
	SurvivePlaybackData *driver = _driver;
	if (driver->playback_file)
		fclose(driver->playback_file);
	driver->playback_file = 0;

	return 0;
}

static int LoadConfig(SurvivePlaybackData *sv, SurviveObject *so) {
	SurviveContext *ctx = sv->ctx;
	char *ct0conf = 0;

	char fname[100];
	sprintf(fname, "%s/%s_config.json", sv->playback_dir, so->codename);
	FILE *f = fopen(fname, "r");

	if (f == 0 || feof(f) || ferror(f))
		return 1;

	fseek(f, 0, SEEK_END);
	int len = ftell(f);
	fseek(f, 0, SEEK_SET); // same as rewind(f);

	ct0conf = malloc(len + 1);
	int read = fread(ct0conf, len, 1, f);
	fclose(f);
	ct0conf[len] = 0;

	printf("Loading config: %d\n", len);
	int rtn = survive_load_htc_config_format(ct0conf, len, so);

	free(ct0conf);

	return rtn;
}

int DriverRegPlayback(SurviveContext *ctx) {
	const char *playback_dir =
		config_read_str(ctx->global_config_values, "PlaybackDir", "");

	if (strlen(playback_dir) == 0) {
		return 0;
	}

	SurvivePlaybackData *sp = calloc(1, sizeof(SurvivePlaybackData));
	sp->ctx = ctx;
	sp->playback_dir = playback_dir;
	sp->time_factor =
		config_read_float(ctx->global_config_values, "PlaybackFactor", 1.);

	printf("%s\n", playback_dir);

	char playback_file[100];
	sprintf(playback_file, "%s/events", playback_dir);
	sp->playback_file = fopen(playback_file, "r");
	if (sp->playback_file == 0) {
		fprintf(stderr, "Could not open playback events file %s",
				playback_file);
		return -1;
	}
	SurviveObject *hmd = survive_create_hmd(ctx, "Playback", sp);
	SurviveObject *wm0 = survive_create_wm0(ctx, "Playback", sp, 0);
	SurviveObject *wm1 = survive_create_wm1(ctx, "Playback", sp, 0);
	SurviveObject *tr0 = survive_create_tr0(ctx, "Playback", sp);
	SurviveObject *ww0 = survive_create_ww0(ctx, "Playback", sp);

	SurviveObject *objs[] = {hmd, wm0, wm1, tr0, ww0, 0};

	for (SurviveObject **obj = objs; *obj; obj++) {
		if (!LoadConfig(sp, *obj)) {
			survive_add_object(ctx, *obj);
		} else {
			free(*obj);
		}
	}

	survive_add_driver(ctx, sp, playback_poll, playback_close, 0);
	return 0;
fail_gracefully:
	return -1;
}

REGISTER_LINKTIME(DriverRegPlayback);
