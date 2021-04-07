/*
 * rgb_color.h
 *
 *  Created on: Aug 21, 2019
 *      Author: kz
 */

#ifndef RGB_COLOR_H_
#define RGB_COLOR_H_

//#include <stdio.h>

typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} rgb_t;

void new_color_brgh(rgb_t *col, uint8_t brgh);
int8_t cmp_color(rgb_t color_1, rgb_t color_2);
void sprint_color(char *buff, rgb_t color);

#endif /* RGB_COLOR_H_ */
