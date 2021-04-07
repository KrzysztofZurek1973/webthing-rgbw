/* *********************************************************
 * LED RGB+White controller - additional functions
 * Compatible with Web Thing API
 *
 *  Created on:		Apr 5, 2021
 * Last update:		Apr 5, 2021
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

 
/****************************************************
 *
 * jsonize model of RGB color property
 *
 * **************************************************/
char *color_model_jsonize(property_t *p){
	char *buff;

	//only unit printed in model
	buff = malloc(12 + strlen(p -> unit));
	sprintf(buff, "\"unit\":\"%s\",", p -> unit);

	return buff;
}


/****************************************************
 *
 * RGB color jsonize
 *
 * **************************************************/
char *color_value_jsonize(property_t *p){
	char *buff; 
	char col_str[] = "\"%s\":\"%s\"";

	buff = malloc(32);
	memset(buff, 0 , 32);
	sprintf(buff, col_str, p -> id, p -> value);

	return buff;
}


/*******************************************************************
 *
 * initialize GPIOs for channel A and B, both switch OFF
 *
 * ******************************************************************/
void init_ledc(void){
	
	//timer configuration
    ledc_timer_config_t ledc_timer = {
        	.duty_resolution = LEDC_TIMER_13_BIT, 	// resolution of PWM duty
	        .freq_hz = 1000,                      	// frequency of PWM signal
    	    .speed_mode = LEDC_HIGH_SPEED_MODE,		// timer mode
    	    .timer_num = LEDC_TIMER_0,            	// timer index
    	    .clk_cfg = LEDC_AUTO_CLK,              	// Auto select the source clock
    };
    // Set configuration of timer0 for high speed channels
    ledc_timer_config(&ledc_timer);
    
    //channel configuration
    //---- channel RED
    ledc_channel_config_t channel_red = {
            .channel    = LEDC_CH_RED,
            .duty       = 0,
            .gpio_num   = GPIO_CH_RED,
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0,
            .intr_type	= LEDC_INTR_DISABLE,
    };
    // Set LED Controller with previously prepared configuration
    ledc_channel_config(&channel_red);
    
    //---- channel GREEN
    ledc_channel_config_t channel_green = {
            .channel    = LEDC_CH_GREEN,
            .duty       = 0,
            .gpio_num   = GPIO_CH_GREEN,
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0,
            .intr_type	= LEDC_INTR_DISABLE,
    };
    // Set LED Controller with previously prepared configuration
    ledc_channel_config(&channel_green);
    
    //---- channel BLUE
    ledc_channel_config_t channel_blue = {
            .channel    = LEDC_CH_BLUE,
            .duty       = 0,
            .gpio_num   = GPIO_CH_BLUE,
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0,
            .intr_type	= LEDC_INTR_DISABLE,
    };
    // Set LED Controller with previously prepared configuration
    ledc_channel_config(&channel_blue);
    
    //---- channel BLUE
    ledc_channel_config_t channel_white = {
            .channel    = LEDC_CH_WHITE,
            .duty       = 0,
            .gpio_num   = GPIO_CH_WHITE,
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_TIMER_0,
            .intr_type	= LEDC_INTR_DISABLE,
    };
    // Set LED Controller with previously prepared configuration
    ledc_channel_config(&channel_white);
    
    // Initialize fade service.
    ledc_fade_func_install(ESP_INTR_FLAG_IRAM);
}
