// (C) 2017 <>< Joshua Allen, Under MIT/x11 License.
#include <string.h>
#include <assert.h>
#include "survive_config.h"
#include <json_helpers.h>

#define MAX_CONFIG_ENTRIES 100
#define MAX_LIGHTHOUSES 2



config_group global_config_values;
config_group lh_config[MAX_LIGHTHOUSES]; //lighthouse configs

//static uint16_t used_entries = 0;

static FILE *config_file = NULL;
const FLT* config_set_float_a(config_group *cg, const char *tag, const FLT* values, uint8_t count);

void init_config_group(config_group *cg, uint16_t count) {
	uint16_t i = 0;
	cg->config_entries = malloc(count*sizeof(config_entry));
	cg->used_entries = 0;
	cg->max_entries = count;

	for (i=0;i<count;++i) {
		cg->config_entries[i].data = NULL;
		cg->config_entries[i].tag = NULL;
		cg->config_entries[i].type = CONFIG_UNKNOWN;
		cg->config_entries[i].elements = 0;
	}
}

void config_init() {
	uint16_t i = 0;
	init_config_group(&global_config_values, MAX_CONFIG_ENTRIES);
	for(i=0;i<MAX_LIGHTHOUSES;i++) {
		init_config_group(lh_config+i, 9);
	}
}

void config_load(const char* path) {
	config_file = fopen(path, "r");
}

void config_close() {
	fclose(config_file);
}

void config_set_lighthouse(struct BaseStationData* bsd, uint8_t idx) {
	config_group *cg = lh_config+idx;
	config_set_uint32(cg,"index", idx);
	config_set_uint32(cg,"id", bsd->BaseStationID);
	config_set_float_a(cg,"position", bsd->Position, 3);
	config_set_float_a(cg,"quaternion", bsd->Quaternion, 4);
	config_set_float_a(cg,"fcalphase", bsd->fcalphase, 2);
	config_set_float_a(cg,"fcaltilt", bsd->fcaltilt,2);
	config_set_float_a(cg,"fcalcurve", bsd->fcalcurve,2);
	config_set_float_a(cg,"fcalgibpha", bsd->fcalgibpha,2);
	config_set_float_a(cg,"fcalgibmag", bsd->fcalgibmag,2);
}

void sstrcpy(char** dest, const char *src) {
	uint32_t len = strlen(src)+1;
	assert(dest!=NULL);

	if (*dest == NULL) {
		*dest = (char*)malloc(len);
	} else {
		*dest = (char*)realloc(*dest, len);
	}
	strcpy(*dest,src);
}

config_entry* find_config_entry(config_group *cg, const char *tag) {
	uint16_t i = 0;
	for (i=0;i < cg->used_entries;++i) {
		if ( strcmp(cg->config_entries[i].tag, tag) == 0 ) {
			return cg->config_entries+i;
		}
	}
	return NULL;
}

const char* config_read_str(config_group *cg, const char *tag, const char *def) {
	config_entry *cv = find_config_entry(cg, tag);

	if (cv != NULL) return cv->data;

	return config_set_str(cg,tag,def);
}

uint32_t config_read_uint32(config_group *cg, const char *tag, const uint32_t def) {
	config_entry *cv = find_config_entry(cg, tag);

	if (cv != NULL) return cv->numeric.i;

	return config_set_uint32(cg, tag, def);
}

FLT config_read_float(config_group *cg, const char *tag, const FLT def) {
	config_entry *cv = find_config_entry(cg, tag);

	if (cv != NULL) return cv->numeric.f;

	return config_set_float(cg, tag, def);
}

config_entry* next_unused_entry(config_group *cg) {
	config_entry *cv = cg->config_entries + cg->used_entries;
	assert(cg->used_entries < cg->max_entries);

/*
	if (cg->used_entries >= cg->max_entries) {
		cg->max_entries+=10;
		cg->config_entries = realloc(cg->config_entries, sizeof(config_entry)*cg->max_entries);
	}
*/

	cg->used_entries++;
	return cv;
}

const char* config_set_str(config_group *cg, const char *tag, const char* value) {
	config_entry *cv = find_config_entry(cg, tag);
	if (cv == NULL) cv = next_unused_entry(cg);

	sstrcpy(&(cv->tag), tag);
	sstrcpy(&(cv->data), value);
	cv->type = CONFIG_STRING;

	return value;
}

const uint32_t config_set_uint32(config_group *cg, const char *tag, const uint32_t value) {
	config_entry *cv = find_config_entry(cg, tag);
	if (cv == NULL) cv = next_unused_entry(cg);

	sstrcpy(&(cv->tag), tag);
	cv->numeric.i = value;
	cv->type = CONFIG_UINT32;

	return value;
}

const FLT config_set_float(config_group *cg, const char *tag, const FLT value) {
	config_entry *cv = find_config_entry(cg, tag);
	if (cv == NULL) cv = next_unused_entry(cg);

	sstrcpy(&(cv->tag), tag);
	cv->numeric.f = value;
	cv->type = CONFIG_FLOAT;

	return value;
}

const FLT* config_set_float_a(config_group *cg, const char *tag, const FLT* values, uint8_t count) {
	config_entry *cv = find_config_entry(cg, tag);
	if (cv == NULL) cv = next_unused_entry(cg);

	sstrcpy(&(cv->tag), tag);

	if (cv->data == NULL) {
		cv->data = (char*)malloc(sizeof(FLT)*count);
	}
	else {
		cv->data = (char*)realloc(cv->data, sizeof(FLT)*count);
	}
	printf("float array\n");

	memcpy(cv->data,values,sizeof(FLT)*count);
	cv->type = CONFIG_FLOAT_ARRAY;
	cv->elements = count;

	return values;
}

void _json_write_float_array(FILE* f, const char* tag, FLT* v, uint8_t count) {
	#ifdef USE_DOUBLE
		json_write_double_array(f,tag,v,count);
	#else
		json_write_float_array(f,tag,v,count);
	#endif
}

void write_config_group(FILE* f, config_group *cg, char *tag) {
	uint16_t i = 0;

	if (tag != NULL) {
		fprintf(f, "\"%s\":{\n", tag);
	}

	for (i=0;i < cg->used_entries;++i) {
		if (cg->config_entries[i].type == CONFIG_FLOAT) {
			json_write_float(f, cg->config_entries[i].tag, cg->config_entries[i].numeric.f);
		} else if (cg->config_entries[i].type == CONFIG_UINT32) {
			json_write_uint32(f, cg->config_entries[i].tag, cg->config_entries[i].numeric.i);
		} else if (cg->config_entries[i].type == CONFIG_STRING) {
			json_write_str(f, cg->config_entries[i].tag, cg->config_entries[i].data);
		} else if (cg->config_entries[i].type == CONFIG_FLOAT_ARRAY) {
			_json_write_float_array(f, cg->config_entries[i].tag, (FLT*)cg->config_entries[i].data, cg->config_entries[i].elements);
		}
		if ((i+1) < cg->used_entries) fprintf(f,",");
		fprintf(f,"\n");
	};

	if (tag != NULL) {
		fprintf(f,"}\n");
	}
}

void config_save(const char* path) {
	uint16_t i = 0;

	FILE* f = fopen(path, "w");

	write_config_group(f,&global_config_values, NULL);
	write_config_group(f,lh_config, "lighthouse0");
	write_config_group(f,lh_config+1, "lighthouse1");

	fclose(f);
}

void print_json_value(char* tag, char** values, uint16_t count) {
	uint16_t i = 0;
	for (i=0;i<count; ++i) {
		printf("%s:%s \n", tag, values[i]);
	}
}

config_group* cg_stack[10]; //handle 10 nested objects deep
uint8_t cg_stack_head = 0;

void handle_config_group(char* tag) {
	cg_stack_head++;
	if (strcmp("lighthouse0",tag) == 0) {
		cg_stack[cg_stack_head] = lh_config;
	} else if (strcmp("lighthouse1",tag) == 0) {
		cg_stack[cg_stack_head] = lh_config+1;
	} else {
		cg_stack[cg_stack_head] = &global_config_values;
	}
}

void pop_config_group() {
	cg_stack_head--;
}

void handle_tag_value(char* tag, char** values, uint16_t count) {
	print_json_value(tag,values,count);
	config_group* cg = cg_stack[cg_stack_head];

	//parse out numeric values

	if (count == 1) config_set_str(cg,tag,values[0]);
//	else if (count>1) config_set_str
}

void config_read(const char* path) {
	json_begin_object = handle_config_group;
	json_end_object = pop_config_group;
	json_tag_value = handle_tag_value;

	cg_stack[0] = &global_config_values;

	json_load_file(path);

	json_begin_object = NULL;
	json_end_object = NULL;
	json_tag_value = NULL;
}

