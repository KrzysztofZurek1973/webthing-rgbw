/*
 * rgb_color.c
 *
 *  Created on: Aug 21, 2019
 *      Author: kz
 */
#include <stdio.h>
#include <stdint.h>

#include "rgb_color.h"

/********************************************
*
* output: 0 .. 100
*
*********************************************/
void new_color_brgh(rgb_t *col, uint8_t brgh){
    uint16_t temp;

    if (brgh > 0){
    	temp = ((uint16_t)(col -> green) * (uint16_t)(brgh)) >> 8;
      	col -> green = (uint8_t)temp;

		temp = ((uint16_t)(col -> red) * (uint16_t)(brgh)) >> 8;
	    col -> red = (uint8_t)temp;
	
		temp = ((uint16_t)(col -> blue) * (uint16_t)(brgh)) >> 8;
		col -> blue = (uint8_t)temp;
    }
    else{
    	col -> green = 0;
    	col -> red = 0;
    	col -> blue = 0;
    }
}


/**********************************************
*
* output:
*	 0	- colors are equal
*	-1	- colors are NOT equal
*
***********************************************/
int8_t cmp_color(rgb_t color_1, rgb_t color_2){
	int8_t result = 0;
	
	if (color_1.red != color_2.red){
		result = -1;
	}
	if (color_1.green != color_2.green){
		result = -1;
	}
	if (color_1.blue != color_2.blue){
		result = -1;
	}
	return result;
}


/*******************************************
*
* convert color into "#aabbcc" format where
*	aa - red, bb - green, cc - blue
*
*******************************************/
void sprint_color(char *buff, rgb_t color){
	sprintf(buff, "#%02x%02x%02x", color.red, color.green, color.blue);
}
