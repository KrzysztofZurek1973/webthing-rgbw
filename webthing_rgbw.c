/* *********************************************************
 * LED RGB+White controller
 * Compatible with Web Thing API
 *
 *  Created on:		Apr 5, 2021
 * Last update:		Apr 7, 2021
 *      Author:		Krzysztof Zurek
 *		E-mail:		krzzurek@gmail.com
 		   www:		www.alfa46.com
 *
 ************************************************************/
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "simple_web_thing_server.h"
#include "webthing_rgbw.h"
#include "rgb_color.h"

#define FADE_TIME 1000

typedef enum {
				MODE_COLOR_WHITE =	0,
				MODE_COLOR =		1,
				MODE_WHITE =		2
			} rgb_mode_t;

xSemaphoreHandle webthing_mux;
xTaskHandle led_task;

static bool DRAM_ATTR fade_is_running = false;
static bool init_data_sent = false;
static bool timer_is_running = false;
static bool sensor_active = false;
static rgb_t current_color;
static uint8_t current_pattern = 0;
static uint8_t pulse_min = 20;
static uint8_t speed = 50;
static rgb_mode_t mode;
static TimerHandle_t fade_timer = NULL;
static TimerHandle_t timer = NULL;
static TimerHandle_t action_timer = NULL;

//THINGS AND PROPERTIES
//------------------------------------------------------------
//--- Thing
thing_t *leds = NULL;
at_type_t leds_type;

//------  property "on" - ON/OFF state
static bool device_is_on = false;
property_t *prop_on;
at_type_t on_prop_type;
int16_t set_on_off(char *name, char *new_value_str); //switch ON/OFF
char on_prop_id[] = "on";
char on_prop_disc[] = "on-off state";
char on_prop_attype_str[] = "OnOffProperty";
char on_prop_title[] = "ON/OFF";

//------  property "white brightness"
static uint32_t white_brightness; //0..100 in percent
property_t *prop_brgh_white;
at_type_t brgh_white_type;

//------  property "rgb brightness"
static uint32_t rgb_brightness; //0..100 in percent
property_t *prop_brgh_rgb;
at_type_t brgh_rgb_type;

//-------- property "color_1"
static char color_1[] = "#00ff00"; //current color
rgb_t color_1_rgb;
property_t *prop_color_1;
at_type_t color_1_prop_type;

//-------- property "color_2"
static char color_2[] = "#0000ff"; //current color
property_t *prop_color_2;
rgb_t color_2_rgb;
at_type_t color_2_prop_type;

//------ action "settings"
action_t *action_settings;
action_input_prop_t *input_timer, *input_mode, *input_pattern, *input_sensor;
action_input_prop_t *input_speed, *input_pulse_min;
enum_item_t enum_mode_rgb, enum_mode_rgb_white, enum_mode_white;
enum_item_t patt_1, patt_2, patt_3, patt_4;
at_type_t settings_attype;

//task function
void leds_fun(void *param); //thread function

//other functions
void read_nvs_data(bool read_default);
void write_nvs_data(void);
void fade_timer_fun(TimerHandle_t xTimer);
int16_t timer_run(int32_t delay);
int32_t set_mode(rgb_mode_t mode);
int8_t fade_up_channel(ledc_channel_t ch, int32_t brgh, int32_t ft);

/******************************************************
 *
 * Complete action
 *
 * *****************************************************/
void action_timer_fun(TimerHandle_t xTimer){
	
	printf("Timer fun: Complete action\n");
	
	complete_action(0, "settings", ACT_COMPLETED);
	
	xTimerDelete(xTimer, 100);
}


/**********************************************************
 *
 * run action function
 * inputs:
 * 		- mode
 *		- pattern
 *		- pulse-min: (minimum brightness in "pulse" patterns as percent
 *					of maximum value)
 *		- activate sensor (boolean value)
 *		- speed: 0..100, in patterns speed of color change
 * 		- timer delay
 *
 * *******************************************************/

int16_t action_run(char *inputs){
	int32_t delay = 0, len;
	rgb_mode_t new_mode;
	int16_t inputs_exec = -1;
	char *p1, *p2, buff[20];

	printf("Run inputs: %s\n", inputs);

	//get timer delay ------------------------------------------------
	p1 = strstr(inputs, "timer");
	if (p1 == NULL) goto timer_run_mode;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_mode;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1);
	}
	else{
		len = p2 - p1 - 1;
	}
	memset(buff, 0, 20);
	memcpy(buff, p1 + 1, len);
	delay = atoi(buff);
	
	if (delay > 0){
		if (timer_run(delay) >= 0){
			inputs_exec++;
			printf("timer started: %i\n", delay);
		}
	}

	//get mode --------------------------------------------------------
timer_run_mode:
	p1 = strstr(inputs, "mode");
	if (p1 == NULL) goto timer_run_sensor;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_sensor;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1);
	}
	else{
		len = p2 - p1 - 1;
	}
	if (strstr(p1 + 1, "COLOR+WHITE") != NULL){
		new_mode = 0;
	}
	else if (strstr(p1 + 1, "COLOR") != NULL){
		new_mode = 1;
	}
	else if (strstr(p1 + 1, "WHITE") != NULL){
		new_mode = 2;
	}
	else{
		goto timer_run_sensor;
	}
	if (set_mode(new_mode) >= 0){
		inputs_exec++;
	}
	printf("mode: %i\n", new_mode);
	
	//get sensor active --------------------------------------------------------
timer_run_sensor:
	p1 = strstr(inputs, "sensor");
	if (p1 == NULL) goto timer_run_pattern;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_pattern;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1);
	}
	else{
		len = p2 - p1 - 1;
	}
	memset(buff, 0, 20);
	memcpy(buff, p1 + 1, len);
	p1 = strstr(buff, "true");
	if (p1 != NULL){
		sensor_active = true;
	}
	
	if (sensor_active == true){
		printf("sensor ACTIVE\n");
	}
	else{
		printf("sensor NOT ACTIVE\n");
	}
	
	inputs_exec++;

	//get pattern ---------------------------------------------------
timer_run_pattern:
	p1 = strstr(inputs, "pattern");
	if (p1 == NULL) goto timer_run_end;
	p1 = strchr(p1, ':');
	if (p1 == NULL) goto timer_run_end;
	p2 = strchr(p1, ',');
	if (p2 == NULL){
		len = strlen(p1 + 1) - 2;
	}
	else{
		len = p2 - p1 - 3;
	}
	if (len > 19){
		len = 19;
	}
	memset(buff, 0, 20);
	memcpy(buff, p1 + 2, len);
	inputs_exec++;
	printf("pattern: %s\n", buff);

timer_run_end:
	if (inputs_exec >= 0){
		//start timer
		action_timer = xTimerCreate("action_timer",
									pdMS_TO_TICKS(1000),
									pdFALSE,
									pdFALSE,
									action_timer_fun);
		//xSemaphoreGive(led_mux);
		
		if (xTimerStart(action_timer, 5) == pdFAIL){
			printf("action timer failed\n");
		}
	}
	printf("run return: %i\n", inputs_exec);
	
	return inputs_exec;
}


/****************************************
*
* set new mode: RGB+WHITE, RGB or WHITE
*
*****************************************/
int32_t set_mode(rgb_mode_t new_mode){
	int32_t result = 0;
	rgb_mode_t prev_mode;
	rgb_t new_color;
	
	prev_mode = mode;
	printf("prev_mode: %i, new_mode %i\n", prev_mode, new_mode);
	if (prev_mode != new_mode){
		xSemaphoreTake(webthing_mux, portMAX_DELAY);
		mode = new_mode;
		switch (prev_mode){
			case MODE_COLOR_WHITE:
				if (new_mode == MODE_COLOR){
					fade_up_channel(LEDC_CH_WHITE, 0, FADE_TIME);
				}
				else{
					fade_up_channel(LEDC_CH_GREEN, 0, FADE_TIME);
					fade_up_channel(LEDC_CH_RED, 0, FADE_TIME);
					fade_up_channel(LEDC_CH_BLUE, 0, FADE_TIME);
				}
				break;
					
			case MODE_COLOR:
				fade_up_channel(LEDC_CH_WHITE, white_brightness, FADE_TIME);
				if (new_mode == MODE_WHITE){
					fade_up_channel(LEDC_CH_GREEN, 0, FADE_TIME);
					fade_up_channel(LEDC_CH_RED, 0, FADE_TIME);
					fade_up_channel(LEDC_CH_BLUE, 0, FADE_TIME);
				}
				break;
				
			case MODE_WHITE:
			default:
				new_color = current_color;
				new_color_brgh(&new_color, rgb_brightness);
				fade_up_channel(LEDC_CH_GREEN, new_color.green, FADE_TIME);
				fade_up_channel(LEDC_CH_RED, new_color.red, FADE_TIME);
				fade_up_channel(LEDC_CH_BLUE, new_color.blue, FADE_TIME);
				if (new_mode == MODE_COLOR){
					fade_up_channel(LEDC_CH_WHITE, 0, FADE_TIME);
				}
		}
		xSemaphoreGive(webthing_mux);
	}

	return result;
}


/* ******************************************************************
 *
 * set color for some patterns
 *
 * ******************************************************************/
int16_t color_set(char *name, char *color){
	int8_t res = 0;
	uint8_t red8, green8, blue8;
	char c[3];

	printf("set color, name: %s, color: %s\n", name, color);

	//convert RGB color in format "#1223ab" into number for every color
	c[2] = 0;
	//RED
	c[0] = color[2];
	c[1] = color[3];
	red8 = (unsigned char)strtol(c, NULL, 16);
	//GREEN
	c[0] = color[4];
	c[1] = color[5];
	green8 = (unsigned char)strtol(c, NULL, 16);
	//BLUE
	c[0] = color[6];
	c[1] = color[7];
	blue8 = (unsigned char)strtol(c, NULL, 16);
	
	if (strstr(name, prop_color_1 -> id) != NULL){
		xSemaphoreTake(webthing_mux, portMAX_DELAY);
		memcpy(color_1, color + 1, 7);
		color_1_rgb.red = red8;
		color_1_rgb.green = green8;
		color_1_rgb.blue = blue8;
		current_color = color_1_rgb;
		xSemaphoreGive(webthing_mux);
		
		if (device_is_on == true){
			if ((mode == MODE_COLOR_WHITE) || (mode == MODE_COLOR)){
				rgb_t new_color;
				new_color = current_color;
				new_color_brgh(&new_color, rgb_brightness);
				fade_up_channel(LEDC_CH_GREEN, new_color.green, FADE_TIME);
				fade_up_channel(LEDC_CH_RED, new_color.red, FADE_TIME);
				fade_up_channel(LEDC_CH_BLUE, new_color.blue, FADE_TIME);
			}
		}
		res = 1;
	}
	else if (strstr(name, prop_color_2 -> id) != NULL){
		xSemaphoreTake(webthing_mux, portMAX_DELAY);
		memcpy(color_2, color + 1, 7);
		color_2_rgb.red = red8;
		color_2_rgb.green = green8;
		color_2_rgb.blue = blue8;
		xSemaphoreGive(webthing_mux);
		res = 1;
	}

	return res;
}



/***********************************************************
*
* fade up one channel
* inputs"
*	- ch - channel number
*	- brgh - brightness [0 .. 100]
*	- ft - fade time [miliseconds]
*
************************************************************/
int8_t fade_up_channel(ledc_channel_t ch, int32_t brgh, int32_t ft){
	int32_t duty;
	
	if (brgh == 100){
		duty = 8191;
	}
	else if (brgh != 0){
		duty = (brgh * 8191)/100;
	}
	else{
		duty = 0;
	}
	//fade_counter++;
	ledc_set_fade_with_time(LEDC_HIGH_SPEED_MODE, ch, duty, (uint32_t)ft);
    ledc_fade_start(LEDC_HIGH_SPEED_MODE, ch, LEDC_FADE_NO_WAIT);
    
    if (fade_is_running == false){
    	fade_is_running = true;
    	//unblock "fade_is_ruuning" after fade finished
   		fade_timer = xTimerCreate("fade_timer",
								pdMS_TO_TICKS(ft) + 5,
								pdFALSE,
								pdFALSE,
								fade_timer_fun);
	
		if (xTimerStart(fade_timer, 0) == pdFAIL){
			printf("fade timer failed\n");
		}
	}
    
    return 1;
}


/*****************************************
 *
 * fade timer finished
 *
 ******************************************/
void fade_timer_fun(TimerHandle_t xTimer){
	
	xSemaphoreTake(webthing_mux, portMAX_DELAY);
	fade_is_running = false;
	xSemaphoreGive(webthing_mux);
	
	xTimerDelete(xTimer, 10); //delete timer
}


/* ****************************************************************
 *
 * set brightness
 *
 * output:
 *		0 - value not changed
 *		1 - new value set, all clients will be informed
 *	   -1 - error
 *
 * ****************************************************************/
int16_t brightness_set(char *name, char *new_value_str){
	int32_t brgh;
	int16_t result = 0;
	
	xSemaphoreTake(webthing_mux, portMAX_DELAY);
	
	if (fade_is_running == true){
		xSemaphoreGive(webthing_mux);
		return -1;
	}

	brgh = atoi(new_value_str);
	if (brgh > 100){
		brgh = 100;
	}
	else if (brgh < 0){
		brgh = 0;
	}
	
	printf("set brgh, new: %i, oldw: %i, oldc: %i\n", brgh,
			white_brightness, rgb_brightness);

	if (strstr(name, prop_brgh_white -> id) != NULL){
		if (white_brightness != brgh){
			white_brightness = brgh;
			if (device_is_on == true){
				if ((mode == MODE_WHITE) || (mode == MODE_COLOR_WHITE)){
					fade_up_channel(LEDC_CH_WHITE, brgh, FADE_TIME);				
				}
			}
			result = 1;
		}
	}
	else if (strstr(name, prop_brgh_rgb -> id) != NULL){
		if (rgb_brightness != brgh){
			rgb_brightness = brgh;
			if (device_is_on == true){
				if ((mode == MODE_COLOR) || (mode == MODE_COLOR_WHITE)){
					rgb_t new_color;
					new_color = current_color;
					new_color_brgh(&new_color, brgh);

					fade_up_channel(LEDC_CH_GREEN, new_color.green, FADE_TIME);
					fade_up_channel(LEDC_CH_RED, new_color.red, FADE_TIME);
					fade_up_channel(LEDC_CH_BLUE, new_color.blue, FADE_TIME);
				}
			}
			result = 1;
		}
	}
	else{
		return -1;
	}
	
	//fade_counter = 0;
	xSemaphoreGive(webthing_mux);

	return result;
}



/* *****************************************************************
 *
 * turn the device ON or OFF
 *
 * output:
 *		0 - value not changed
 *		1 - new value set, all clients will be informed
 *	   -1 - error
 *
 * *****************************************************************/
int16_t set_on_off(char *name, char *new_value_str){
	int32_t white_brgh = 0, color_brgh = 0;
	bool state_change = false;
	int16_t result = 0;
	
	printf("Set ON/OFF, name: %s, input: %s\n", name, new_value_str);

	xSemaphoreTake(webthing_mux, portMAX_DELAY);
	if (fade_is_running == true){
		//fade action is running
		xSemaphoreGive(webthing_mux);
		return -1;
	}
	
	if (strcmp(new_value_str, "true") == 0){
		//switch ON
		if (device_is_on == false){
			device_is_on = true;
			white_brgh = white_brightness;
			color_brgh = rgb_brightness;
			state_change = true;
		}
	}
	else if (strcmp(new_value_str, "false") == 0){
		//switch OFF
		if (device_is_on == true){
			device_is_on = false;
			white_brgh = 0;
			color_brgh = 0;
			state_change = true;
		}
	}
	else{
		//error
		xSemaphoreGive(webthing_mux);
		return -1;
	}
	
	if (state_change == true){
		//turn ON/OFF
		rgb_t new_color;
		new_color = current_color;
		new_color_brgh(&new_color, color_brgh);
		switch (mode){
			case MODE_COLOR_WHITE:
				fade_up_channel(LEDC_CH_WHITE, white_brgh, FADE_TIME);
				fade_up_channel(LEDC_CH_GREEN, new_color.green, FADE_TIME);
				fade_up_channel(LEDC_CH_RED, new_color.red, FADE_TIME);
				fade_up_channel(LEDC_CH_BLUE, new_color.blue, FADE_TIME);
				break;
					
			case MODE_COLOR:
				fade_up_channel(LEDC_CH_GREEN, new_color.green, FADE_TIME);
				fade_up_channel(LEDC_CH_RED, new_color.red, FADE_TIME);
				fade_up_channel(LEDC_CH_BLUE, new_color.blue, FADE_TIME);
				break;
				
			case MODE_WHITE:
			default:
				//start only white channel
				fade_up_channel(LEDC_CH_WHITE, white_brgh, FADE_TIME);
		}
		if (device_is_on == false){
			write_nvs_data();
		}
		result = 1;
	}
	else{
		result = 0;
	}
	
	xSemaphoreGive(webthing_mux);	
	
	return result;
}


/******************************************************
 *
 * timer is finished, turn all channels OFF
 *
 * *****************************************************/
void timer_fun(TimerHandle_t xTimer){
	bool state_changed = false;
	
	complete_action(0, "timer", ACT_COMPLETED);
	
	xSemaphoreTake(webthing_mux, portMAX_DELAY);

	if (device_is_on == true){
		//switch OFF all channels
		device_is_on = false;
		fade_up_channel(LEDC_CH_WHITE, 0, FADE_TIME);
		fade_up_channel(LEDC_CH_GREEN, 0, FADE_TIME);
		fade_up_channel(LEDC_CH_RED, 0, FADE_TIME);
		fade_up_channel(LEDC_CH_BLUE, 0, FADE_TIME);
		state_changed = true;
	}
	xSemaphoreGive(webthing_mux);
	
	xTimerDelete(xTimer, 100); //delete timer
	timer_is_running = false;
	
	if (state_changed == true){
		inform_all_subscribers_prop(prop_on);
		//copy current poropeties values
		int32_t prev_white_brgh = white_brightness;
		int32_t prev_rgb_brgh = rgb_brightness;
		rgb_t curr_color_1_rgb = color_1_rgb;
		rgb_t curr_color_2_rgb = color_2_rgb;

		//read values from NVS memory
		read_nvs_data(false);

		//if values from NVS are different then the last set inform clients		
		if (prev_white_brgh != white_brightness){
			inform_all_subscribers_prop(prop_brgh_white);
		}
		if (prev_rgb_brgh != rgb_brightness){
			inform_all_subscribers_prop(prop_brgh_rgb);
		}
		if (cmp_color(curr_color_1_rgb, color_1_rgb) != 0){
			inform_all_subscribers_prop(prop_color_1);
		}
		if (cmp_color(curr_color_2_rgb, color_2_rgb) != 0){
			inform_all_subscribers_prop(prop_color_2);
		}
	}
}


/**********************************************************
 *
 * timer action
 * inputs:
 * 		- minutes to turn OFF all LEDs
 *
 * *******************************************************/
int16_t timer_run(int32_t delay){
	
	if ((delay > 600) || (delay == 0) || (device_is_on == false)){
		goto inputs_error;
	}
	//start timer
	timer = xTimerCreate("timer",
						pdMS_TO_TICKS(delay * 60 * 1000),
						pdFALSE,
						pdFALSE,
						timer_fun);
	
	if (xTimerStart(timer, 5) == pdFAIL){
		printf("timer failed\n");
		goto inputs_error;
	}
	else{
		timer_is_running = true;
	}

	return 0;

	inputs_error:
		printf("timer ERROR\n");
	return -1;
}


/*********************************************************************
 *
 * main task
 *
 * ******************************************************************/
void leds_fun(void *param){
	
	TickType_t last_wake_time = xTaskGetTickCount();
	for (;;){
		last_wake_time = xTaskGetTickCount();
		
		if (init_data_sent == false){
			int8_t s1 = 0;
			s1 += inform_all_subscribers_prop(prop_on);
			s1 += inform_all_subscribers_prop(prop_color_1);
			s1 += inform_all_subscribers_prop(prop_color_2);
			s1 += inform_all_subscribers_prop(prop_brgh_white);
			s1 += inform_all_subscribers_prop(prop_brgh_rgb);
			if (s1 == 0){
				init_data_sent = true;
				printf("init data sent\n");
			}
		}
	
		vTaskDelayUntil(&last_wake_time, APP_PERIOD / portTICK_PERIOD_MS);
	}
}


/*****************************************************************
 *
 * Initialization of the controller
 *
 * ****************************************************************/
thing_t *init_rgbw_controller(void){
	
	read_nvs_data(true);
	
	init_ledc();
	
	//start thing
	webthing_mux = xSemaphoreCreateMutex();
	//create thing 1, thermostat ---------------------------------
	leds = thing_init();
	leds -> id = "RGBW stripe";
	leds -> at_context = things_context;
	leds -> model_len = 2500;
	//set @type
	leds_type.at_type = "Light";
	leds_type.next = NULL;
	set_thing_type(leds, &leds_type);
	leds -> description = "RGB+White controller";
	
	//property: ON/OFF
	prop_on = property_init(NULL, NULL);
	prop_on -> id = "on";
	prop_on -> description = "ON/OFF";
	on_prop_type.at_type = "OnOffProperty";
	on_prop_type.next = NULL;
	prop_on -> at_type = &on_prop_type;
	prop_on -> type = VAL_BOOLEAN;
	prop_on -> value = &device_is_on;
	prop_on -> title = "ON/OFF";
	prop_on -> read_only = false;
	prop_on -> set = set_on_off;
	prop_on -> mux = webthing_mux;
	add_property(leds, prop_on); //add property to thing
	
	//property: rgb brightness
	prop_brgh_rgb = property_init(NULL, NULL);
	prop_brgh_rgb -> id = "brgh-rgb";
	prop_brgh_rgb -> description = "color brightness";
	brgh_rgb_type.at_type = "BrightnessProperty";
	brgh_rgb_type.next = NULL;
	prop_brgh_rgb -> at_type = &brgh_rgb_type;
	prop_brgh_rgb -> type = VAL_INTEGER;
	prop_brgh_rgb -> value = &rgb_brightness;
	prop_brgh_rgb -> max_value.int_val = 100;
	prop_brgh_rgb -> min_value.int_val = 0;
	prop_brgh_rgb -> unit = "percent";
	prop_brgh_rgb -> title = "Color Brightness";
	prop_brgh_rgb -> read_only = false;
	prop_brgh_rgb -> set = brightness_set;
	prop_brgh_rgb -> mux = webthing_mux;
	add_property(leds, prop_brgh_rgb);	
	
	//property: white brightness
	prop_brgh_white = property_init(NULL, NULL);
	prop_brgh_white -> id = "brgh-white";
	prop_brgh_white -> description = "white brightness";
	brgh_white_type.at_type = "BrightnessProperty";
	brgh_white_type.next = NULL;
	prop_brgh_white -> at_type = &brgh_white_type;
	prop_brgh_white -> type = VAL_INTEGER;
	prop_brgh_white -> value = &white_brightness;
	prop_brgh_white -> max_value.int_val = 100;
	prop_brgh_white -> min_value.int_val = 0;
	prop_brgh_white -> unit = "percent";
	prop_brgh_white -> title = "White Brightness";
	prop_brgh_white -> read_only = false;
	prop_brgh_white -> set = brightness_set;
	prop_brgh_white -> mux = webthing_mux;
	add_property(leds, prop_brgh_white);
	
	//property: color_1
	prop_color_1 = property_init(NULL, NULL);
	prop_color_1 -> id = "color-1";
	prop_color_1 -> description = "first color";
	color_1_prop_type.at_type = "ColorProperty";
	color_1_prop_type.next = NULL;
	prop_color_1 -> at_type = &color_1_prop_type;
	prop_color_1 -> type = VAL_STRING;
	prop_color_1 -> value = &color_1;
	prop_color_1 -> unit = "color RGB";
	prop_color_1 -> title = "Color-1";
	prop_color_1 -> read_only = false;
	prop_color_1 -> set = color_set;
	prop_color_1 -> model_jsonize = color_model_jsonize;
	prop_color_1 -> value_jsonize = color_value_jsonize;
	prop_color_1 -> mux = webthing_mux;
	add_property(leds, prop_color_1);
	
	//property: color_2
	prop_color_2 = property_init(NULL, NULL);
	prop_color_2 -> id = "color-2";
	prop_color_2 -> description = "second color";
	color_2_prop_type.at_type = "ColorProperty";
	color_2_prop_type.next = NULL;
	prop_color_2 -> at_type = &color_2_prop_type;
	prop_color_2 -> type = VAL_STRING;
	prop_color_2 -> value = &color_2;
	prop_color_2 -> unit = "color RGB";
	prop_color_2 -> title = "Color-2";
	prop_color_2 -> read_only = false;
	prop_color_2 -> set = color_set;
	prop_color_2 -> model_jsonize = color_model_jsonize;
	prop_color_2 -> value_jsonize = color_value_jsonize;
	prop_color_2 -> mux = webthing_mux;
	add_property(leds, prop_color_2);
	
	//---------------------------------------------
	//create action "settings"
	action_settings = action_init();
	action_settings -> id = "settings";
	action_settings -> title = "SETTINGS";
	action_settings -> description = "More settings";
	action_settings -> run = action_run;
	settings_attype.at_type = "ToggleAction";
	settings_attype.next = NULL;
	action_settings -> input_at_type = &settings_attype;
	
	/*define action input properties
	*  input parameters:
	*	- type, e.g. VAL_INTEGER, VAL_STRING, VAL_BOOLEAN, VAL_NUMBER
	*	- true: required, false: not required
	*	- pointer to minimum value (type int_float_u)
	*	- pointer to maximum value (type int_float_u)
	*	- unit (as char array)
	*	- true: enum values
	*	- pointer to enum list (type enum_item_t)
	*/
	input_sensor = action_input_prop_init("sensor",
											VAL_BOOLEAN,
											false,
											NULL,
											NULL,
											NULL,
											false,
											NULL);
	add_action_input_prop(action_settings, input_sensor);
	
	//timer
	int_float_u d_min, d_max;
	d_min.int_val = 0;
	d_max.int_val = 100;
	input_timer = action_input_prop_init("timer",
										VAL_INTEGER,//type
										false,		//required
										&d_min,		//min value
										&d_max,		//max value
										"minutes",	//unit
										false,		//is enum
										NULL);		//enum list pointer
	add_action_input_prop(action_settings, input_timer);

	//enums
	enum_mode_rgb.value.str_addr = "COLOR+WHITE";
	enum_mode_rgb.next = &enum_mode_rgb_white;
	enum_mode_rgb_white.value.str_addr = "COLOR";
	enum_mode_rgb_white.next = &enum_mode_white;
	enum_mode_white.value.str_addr = "WHITE";
	enum_mode_white.next = NULL;

	input_mode = action_input_prop_init("mode",
										VAL_STRING,
										false,
										NULL,
										NULL,
										NULL,
										true,
										&enum_mode_rgb);
	add_action_input_prop(action_settings, input_mode);

	//pattern names
	patt_1.value.str_addr = "STATIC";
	patt_1.next = &patt_2;
	patt_2.value.str_addr = "RGB";
	patt_2.next = &patt_3;
	patt_3.value.str_addr = "COLOR PULSE";
	patt_3.next = &patt_4;
	patt_4.value.str_addr = "RGB PULSE";
	patt_4.next = NULL;
	input_pattern = action_input_prop_init("pattern",
											VAL_STRING,
											false,
											NULL,
											NULL,
											NULL,
											true,
											&patt_1);
	add_action_input_prop(action_settings, input_pattern);
	
	//speed, how fast colors are changed in "puls" patterns
	int_float_u s_min, s_max;
	s_min.int_val = 0;
	s_max.int_val = 100;
	input_speed = action_input_prop_init("speed",
										VAL_INTEGER,	//type
										false,			//required
										&s_min,			//min value
										&s_max,			//max value
										"percent",		//unit
										false,			//is enum
										NULL);			//enum list pointer
	add_action_input_prop(action_settings, input_speed);
	
	//puls minimum, low value in pulses in percentage of high value
	int_float_u p_min, p_max;
	p_min.int_val = 0;
	p_max.int_val = 100;
	input_pulse_min = action_input_prop_init("pulse-min",
										VAL_INTEGER,	//type
										false,			//required
										&p_min,			//min value
										&p_max,			//max value
										"percent",		//unit
										false,			//is enum
										NULL);			//enum list pointer
	add_action_input_prop(action_settings, input_pulse_min);

	add_action(leds, action_settings);

	//start thread	
	xTaskCreate(&leds_fun, "leds", configMINIMAL_STACK_SIZE * 4, NULL, 5, &led_task);

	return leds;
}


/****************************************************************
 *
 * read data saved in NVS memory:
 *  - mode
 *	- color_1 and color_2
 *	- brightness for white and RGB channels
 *	- sensor activated
 *	- current pattern
 *	- pulse-min
 *	- speed
 *
 * **************************************************************/
void read_nvs_data(bool read_default){
	esp_err_t err;
	nvs_handle storage = 0;

	if (read_default == true){
		//default values
		white_brightness = 20;
		rgb_brightness = 20;
		mode = MODE_COLOR;
		color_1_rgb.red = 255;
		color_1_rgb.green = 0;
		color_1_rgb.blue = 0;
		current_color = color_1_rgb;
		sprint_color(color_1, current_color);
		color_2_rgb.red = 0;
		color_2_rgb.green = 255;
		color_2_rgb.blue = 0;
		sprint_color(color_2, color_2_rgb);
	}

	// Open
	//printf("Reading NVS data... ");

	err = nvs_open("storage", NVS_READWRITE, &storage);
	if (err != ESP_OK) {
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	}
	else {
		uint8_t d8;
		uint32_t u32;
		// Mode
		if (nvs_get_u8(storage, "mode", &d8) != ESP_OK){
			printf("Mode default set in NVS\n");
			nvs_set_u8(storage, "mode", mode);
		}
		else{
			mode = d8;
		}
		//white brightness
		if (nvs_get_u32(storage, "white-brgh", &u32) != ESP_OK){
			printf("white brightness default set in NVS\n");
			nvs_set_u32(storage, "white-brgh", white_brightness);
		}
		else{
			white_brightness = u32;
		}
		//RGB brightness
		if (nvs_get_u32(storage, "rgb-brgh", &u32) != ESP_OK){
			printf("rgb brightness default set in NVS\n");
			nvs_set_u32(storage, "rgb-brgh", white_brightness);
		}
		else{
			rgb_brightness = u32;
		}
		//color-1
		if (nvs_get_u32(storage, "color-1", &u32) != ESP_OK){
			printf("color-1 set default in NVS\n");
			uint32_t def_color = color_1_rgb.red + (color_1_rgb.green << 8) + 
								(color_1_rgb.blue << 16);
			nvs_set_u32(storage, "color-1", def_color);
		}
		else{
			color_1_rgb.red = (uint8_t)u32;
			color_1_rgb.green = (uint8_t)(u32 >> 8);
			color_1_rgb.blue = (uint8_t)(u32 >> 16);
			sprint_color(color_1, color_1_rgb);
			current_color = color_1_rgb;
		}
		//color-2
		if (nvs_get_u32(storage, "color-2", &u32) != ESP_OK){
			printf("color-2 set default in NVS\n");
			uint32_t def_color = color_2_rgb.red + (color_2_rgb.green << 8) + 
								(color_2_rgb.blue << 16);
			nvs_set_u32(storage, "color-2", def_color);
		}
		else{
			color_2_rgb.red = (uint8_t)u32;
			color_2_rgb.green = (uint8_t)(u32 >> 8);
			color_2_rgb.blue = (uint8_t)(u32 >> 16);
			sprint_color(color_2, color_2_rgb);
		}
		// Sensor
		if (nvs_get_u8(storage, "sensor", &d8) != ESP_OK){
			printf("Sensor set dafault in NVS\n");
			nvs_set_u8(storage, "sensor", sensor_active);
		}
		else{
			sensor_active = (bool)d8;
		}
		// Speed
		if (nvs_get_u8(storage, "speed", &d8) != ESP_OK){
			printf("Speed set default in NVS\n");
			nvs_set_u8(storage, "speed", speed);
		}
		else{
			speed = d8;
		}
		// Pulse-min
		if (nvs_get_u8(storage, "pulse-min", &d8) != ESP_OK){
			printf("Pulse-min set default in NVS\n");
			nvs_set_u8(storage, "pulse-min", pulse_min);
		}
		else{
			pulse_min = d8;
		}
		// Pattern
		if (nvs_get_u8(storage, "pattern", &d8) != ESP_OK){
			printf("Pattern not found in NVS\n");
			nvs_set_u8(storage, "pattern", current_pattern);
		}
		else{
			current_pattern = d8;
		}
		
		err = nvs_commit(storage);
		// Close
		nvs_close(storage);
	}
}

/****************************************************************
 *
 * write NVS data into flash memory
 *
 * **************************************************************/
void write_nvs_data(void){
	esp_err_t err;
	nvs_handle storage = 0;
	
	//open NVS falsh memory
	err = nvs_open("storage", NVS_READWRITE, &storage);
	if (err != ESP_OK) {
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	}
	else {
		uint32_t u32;
		uint8_t	d8;
		
		//white brightness ----------------------------------------
		if (nvs_get_u32(storage, "white-brgh", &u32) == ESP_OK){
			if (u32 != white_brightness){
				nvs_set_u32(storage, "white-brgh", white_brightness);
			}
		}
		//RGB brightness -------------------------------------------
		if (nvs_get_u32(storage, "rgb-brgh", &u32) == ESP_OK){
			if (u32 != rgb_brightness){
				nvs_set_u32(storage, "rgb-brgh", rgb_brightness);
			}
		}
		//color-1 -------------------------------------------
		if (nvs_get_u32(storage, "color-1", &u32) == ESP_OK){
			rgb_t c1;
			c1.red = (uint8_t)u32;
			c1.green = (uint8_t)(u32 >> 8);
			c1.blue = (uint8_t)(u32 >> 16);
			if (cmp_color(c1, color_1_rgb) != 0){
				uint32_t c = color_1_rgb.red + (color_1_rgb.green << 8) + 
								(color_1_rgb.blue << 16);
				nvs_set_u32(storage, "color-1", c);
			}
		}
		//color-2 -------------------------------------------
		if (nvs_get_u32(storage, "color-2", &u32) == ESP_OK){
			rgb_t c1;
			c1.red = (uint8_t)u32;
			c1.green = (uint8_t)(u32 >> 8);
			c1.blue = (uint8_t)(u32 >> 16);
			if (cmp_color(c1, color_2_rgb) != 0){
				uint32_t c = color_2_rgb.red + (color_2_rgb.green << 8) + 
								(color_2_rgb.blue << 16);
				nvs_set_u32(storage, "color-2", c);
			}
		}
		//mode ----------------------------------------
		if (nvs_get_u8(storage, "mode", &d8) == ESP_OK){
			if (d8 != mode){
				nvs_set_u8(storage, "mode", mode);
			}
		}
		//sensor ----------------------------------------
		if (nvs_get_u8(storage, "sensor", &d8) == ESP_OK){
			if (d8 != sensor_active){
				nvs_set_u8(storage, "sensor", sensor_active);
			}
		}
		//pattern ----------------------------------------
		if (nvs_get_u8(storage, "pattern", &d8) == ESP_OK){
			if (d8 != current_pattern){
				nvs_set_u8(storage, "pattern", current_pattern);
			}
		}
		//pulse-min ----------------------------------------
		if (nvs_get_u8(storage, "pulse-min", &d8) == ESP_OK){
			if (d8 != pulse_min){
				nvs_set_u8(storage, "pulse-min", pulse_min);
			}
		}
		//speed ----------------------------------------
		if (nvs_get_u8(storage, "speed", &d8) == ESP_OK){
			if (d8 != speed){
				nvs_set_u8(storage, "speed", speed);
			}
		}


		err = nvs_commit(storage);
		// Close
		nvs_close(storage);
	}
}
