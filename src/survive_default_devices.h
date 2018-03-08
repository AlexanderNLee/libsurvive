#ifndef _SURVIVE_DEFAULT_DEVICES_H
#define _SURVIVE_DEFAULT_DEVICES_H

#include <survive.h>

SurviveObject* survive_create_hmd(SurviveContext * ctx, const char* driver_name, void* driver);
SurviveObject* survive_create_wm0(SurviveContext * ctx, const char* driver_name, void* driver, haptic_func cb);
SurviveObject* survive_create_wm1(SurviveContext * ctx, const char* driver_name, void* driver, haptic_func cb);
SurviveObject* survive_create_tr0(SurviveContext * ctx, const char* driver_name, void* driver);
SurviveObject* survive_create_ww0(SurviveContext * ctx, const char* driver_name, void* driver);

int survive_load_htc_config_format(char* ct0conf, int length, SurviveObject * so);
#endif
