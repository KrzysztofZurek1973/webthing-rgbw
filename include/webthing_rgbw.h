/*
 * webthing_rgbw.h
 *
 *  Created on: Apr 05, 2021
 *      Author: Krzysztof Zurek
 *		krzzurek@gmail.com
 */

#ifndef WEBTHING_RGBW_H_
#define WEBTHING_RGBW_H_

#define APP_PERIOD 5000

//relays
#define GPIO_CH_RED			(CONFIG_CH_RED_GPIO)
#define GPIO_CH_GREEN		(CONFIG_CH_GREEN_GPIO)
#define GPIO_CH_BLUE		(CONFIG_CH_BLUE_GPIO)
#define GPIO_CH_WHITE		(CONFIG_CH_WHITE_GPIO)

#define ESP_INTR_FLAG_DEFAULT 0
#define LEDC_CH_RED			LEDC_CHANNEL_0
#define LEDC_CH_GREEN		LEDC_CHANNEL_1
#define LEDC_CH_BLUE		LEDC_CHANNEL_2
#define LEDC_CH_WHITE		LEDC_CHANNEL_3

#include "simple_web_thing_server.h"

//---------------------------------------------------------
thing_t *init_rgbw_controller(void);
char *color_model_jsonize(property_t *p);
char *color_value_jsonize(property_t *p);
void init_ledc(void);

#endif /* WEBTHING_RGBW_H_ */
