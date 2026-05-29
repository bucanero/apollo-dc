/*
# based on orbisPad from ORBISDEV Open Source Project.
# Copyright 2010-2020, orbisdev - http://orbisdev.github.io
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <dc/maple/controller.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONT_LTRIGGER       (1<<16)
#define CONT_RTRIGGER       (1<<17)

#define CONT_L2TRIGGER       (1<<26)
#define CONT_R2TRIGGER       (1<<27)


#define ANALOG_CENTER       0x78
#define ANALOG_THRESHOLD    0x68
#define ANALOG_MIN          (ANALOG_CENTER - ANALOG_THRESHOLD)
#define ANALOG_MAX          (ANALOG_CENTER + ANALOG_THRESHOLD)


typedef struct DCpadConfig
{
	uint32_t padDataCurrent;
	uint32_t padDataLast;
	unsigned int buttonsPressed;
	unsigned int buttonsReleased;
	unsigned int buttonsHold;
	unsigned int idle;
} DCpadConfig;

int dcPadInit(void);
void dcPadFinish(void);
DCpadConfig *dcPadGetConf(void);
bool dcPadGetButtonHold(unsigned int filter);
bool dcPadGetButtonPressed(unsigned int filter);
bool dcPadGetButtonReleased(unsigned int filter);
unsigned int dcPadGetCurrentButtonsPressed(void);
unsigned int dcPadGetCurrentButtonsReleased(void);
void dcPadSetCurrentButtonsPressed(unsigned int buttons);
void dcPadSetCurrentButtonsReleased(unsigned int buttons);
int dcPadUpdate(void);

#ifdef __cplusplus
}
#endif
