/*
 * Copyright (c) 2015-2020 ACOINFO, Inc.
 */

#include <ms_rtos.h>

#include "GUI.h"
#include "DIALOG.h"

WM_HWIN CreateWindow(void);

int main(int argc, char *argv[])
{
	GUI_Init();
	WM_MULTIBUF_Enable(1);
	CreateWindow();

	while (1) {
		GUI_Delay(20);
	}
}
