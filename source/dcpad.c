/*
# based on orbisPad from ORBISDEV Open Source Project.
# Copyright 2010-2020, orbisdev - http://orbisdev.github.io
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <dbglogger.h>
#include <SDL2/SDL.h>
#include "dcpad.h"

#define LOG dbglogger_log

static DCpadConfig dcPadConf;
static int orbispad_initialized = 0;
static uint64_t g_time;
static int sCurrentGameControllerIndex = -1;
static SDL_GameController *sCurrentGameController = NULL;


static SDL_GameController *getGameController(void)
{
    // The game controller is not valid anymore, invalidate.
    if (sCurrentGameControllerIndex != -1
        && SDL_IsGameController(sCurrentGameControllerIndex) == 0)
    {
        sCurrentGameController = NULL;
    }

    if (sCurrentGameController != NULL)
    {
        return sCurrentGameController;
    }

    int numberOfJoysticks = SDL_NumJoysticks();

    for (int i = 0; i < numberOfJoysticks; ++i)
    {
        if (SDL_IsGameController(i))
        {
            sCurrentGameController = SDL_GameControllerOpen(i);
            sCurrentGameControllerIndex = i;
            break;
        }
    }

    return sCurrentGameController;
}

static uint64_t timeInMilliseconds(void)
{
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (((uint64_t)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

void dcPadFinish(void)
{
	if(orbispad_initialized)
	{
		LOG("scePadClose");
	}
	orbispad_initialized=0;

	LOG("ORBISPAD finished");
}

DCpadConfig *dcPadGetConf(void)
{
	if(orbispad_initialized)
	{
		return (&dcPadConf);
	}
	
	return NULL; 
}

static int dcPadInitConf(void)
{	
	if(orbispad_initialized)
	{
		return orbispad_initialized;
	}

	memset(&dcPadConf, 0, sizeof(DCpadConfig));
	
	return 0;
}

unsigned int dcPadGetCurrentButtonsPressed(void)
{
	return dcPadConf.buttonsPressed;
}

void dcPadSetCurrentButtonsPressed(unsigned int buttons)
{
	dcPadConf.buttonsPressed=buttons;
}

unsigned int dcPadGetCurrentButtonsReleased(void)
{
	return dcPadConf.buttonsReleased;
}

void dcPadSetCurrentButtonsReleased(unsigned int buttons)
{
	dcPadConf.buttonsReleased=buttons;
}

bool dcPadGetButtonHold(unsigned int filter)
{
	uint64_t time = timeInMilliseconds();
	uint64_t delta = time - g_time;

	if((dcPadConf.buttonsHold&filter)==filter && delta > 0x100)
	{
		g_time = time;
		return 1;
	}

	return 0;
}

bool dcPadGetButtonPressed(unsigned int filter)
{
	if((dcPadConf.buttonsPressed&filter)==filter)
	{
		dcPadConf.buttonsPressed ^= filter;
		return 1;
	}

	return 0;
}

bool dcPadGetButtonReleased(unsigned int filter)
{
 	if((dcPadConf.buttonsReleased&filter)==filter)
	{
		if(~(dcPadConf.padDataLast)&filter)
		{
			return 0;
		}
		return 1;
	}

	return 0;
}

int dcPadUpdate(void)
{
	unsigned int actualButtons=0;
	unsigned int lastButtons=0;

	dcPadConf.padDataLast = dcPadConf.padDataCurrent;
	SDL_GameControllerUpdate();

/*
	ret = SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_A);
	if (ret) LOG("SDL_CONTROLLER_BUTTON_A: %d", ret);
	ret = SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_B);
	if (ret) LOG("SDL_CONTROLLER_BUTTON_B: %d", ret);
*/
	dcPadConf.padDataCurrent = 0;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_DPAD_UP))
		dcPadConf.padDataCurrent |= CONT_DPAD_UP;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
		dcPadConf.padDataCurrent |= CONT_DPAD_DOWN;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
		dcPadConf.padDataCurrent |= CONT_DPAD_LEFT;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
		dcPadConf.padDataCurrent |= CONT_DPAD_RIGHT;

	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_A))
		dcPadConf.padDataCurrent |= CONT_A;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_B))
		dcPadConf.padDataCurrent |= CONT_B;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_X))
		dcPadConf.padDataCurrent |= CONT_X;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_Y))
		dcPadConf.padDataCurrent |= CONT_Y;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_START))
		dcPadConf.padDataCurrent |= CONT_START;
//	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_BACK))
//		dcPadConf.padDataCurrent |= CONT_DPAD_SELECT;

	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
		dcPadConf.padDataCurrent |= CONT_LTRIGGER;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
		dcPadConf.padDataCurrent |= CONT_RTRIGGER;
/*
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_LEFTSTICK))
		dcPadConf.padDataCurrent |= CONT_DPAD_L3;
	if (SDL_GameControllerGetButton(sCurrentGameController, SDL_CONTROLLER_BUTTON_RIGHTSTICK))
		dcPadConf.padDataCurrent |= CONT_DPAD_R3;
	if (SDL_GameControllerGetAxis(sCurrentGameController, SDL_CONTROLLER_AXIS_TRIGGERLEFT))
		dcPadConf.padDataCurrent |= CONT_DPAD_L2;
	if (SDL_GameControllerGetAxis(sCurrentGameController, SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
		dcPadConf.padDataCurrent |= CONT_DPAD_R2;
*/
/*
		if (buttons.ljoy_v < ANALOG_MIN)
			dcPadConf.padDataCurrent |= CONT_DPAD_UP;

		if (buttons.ljoy_v > ANALOG_MAX)
			dcPadConf.padDataCurrent |= CONT_DPAD_DOWN;

		if (buttons.ljoy_h < ANALOG_MIN)
			dcPadConf.padDataCurrent |= CONT_DPAD_LEFT;

		if (buttons.ljoy_h > ANALOG_MAX)
			dcPadConf.padDataCurrent |= CONT_DPAD_RIGHT;
*/

	actualButtons=dcPadConf.padDataCurrent;
	lastButtons=dcPadConf.padDataLast;
	dcPadConf.buttonsPressed=(actualButtons)&(~lastButtons);
	if(actualButtons!=lastButtons)
	{
		dcPadConf.buttonsReleased=(~actualButtons)&(lastButtons);
		dcPadConf.idle=0;
	}
	else
	{
		dcPadConf.buttonsReleased=0;
		if (actualButtons == 0)
		{
			dcPadConf.idle++;
		}
	}
	dcPadConf.buttonsHold=actualButtons&lastButtons;

	return 0;
}

int dcPadInit(void)
{
	int ret;

	if(dcPadInitConf()==1)
	{
		LOG("ORBISPAD already initialized!");
		return orbispad_initialized;
	}

	ret = SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	if (ret < 0)
	{
		LOG("SDL_InitSubSystem(SDL_INIT_JOYSTICK) failed: %d", ret);
		return -1;
	}
	getGameController();

	orbispad_initialized=1;
	g_time = timeInMilliseconds();
	LOG("ORBISPAD initialized: 0x%X", ret);

	return orbispad_initialized;
}
