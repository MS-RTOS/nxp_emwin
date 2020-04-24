/*
 * Copyright (c) 2019 MS-RTOS Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ms_emwin_porting.c emWin porting.
 *
 * Author: Jiao.jinxing <jiaojixing@acoinfo.com>
 *
 */

#include <ms_rtos.h>

#include "GUI.h"
#include "GUIDRV_Lin.h"

#include <stdlib.h>

/*********************************************************************
*
*       Defines
*
**********************************************************************
*/
//
// Define the available number of bytes available for the GUI
//
#define GUI_NUMBYTES  (16 * 1024)

/*********************************************************************
*
*       Layer configuration (to be modified)
*
**********************************************************************
*/
//
// Color conversion
//
#define COLOR_CONVERSION GUICC_8888

//
// Buffers
//
#define NUM_BUFFERS  1 // Number of multiple buffers to be used

/*********************************************************************
*
* Global data
*/
static ms_handle_t gui_lock_handle;
static ms_handle_t gui_sem_handle;
static int gui_touch_fd;

/*********************************************************************
*
* Timing:
* GUI_X_GetTime()
* GUI_X_Delay(int)
*
* Some timing dependent routines require a GetTime
* and delay function. Default time unit (tick), normally is
* 1 ms.
*/

int GUI_X_GetTime(void)
{
    return ms_time_get_ms();
}

void GUI_X_Delay(int ms)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;
    ms_touch_event_t event;
    GUI_PID_STATE pid_state;
    ms_fd_set_t rfds;
    ms_timeval_t tv;

    FD_ZERO(&rfds);
    FD_SET(gui_touch_fd, &rfds);

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    if (ms_io_select(gui_touch_fd + 1, &rfds, MS_NULL, MS_NULL, &tv) == 1) {
        if (ms_io_read(gui_touch_fd, &event, sizeof(event)) == sizeof(event)) {
            if (event.touch_detected > 0) {
                last_x = event.touch_x[0];
                last_y = event.touch_y[0];

                pid_state.x = event.touch_x[0];
                pid_state.y = event.touch_y[0];
                pid_state.Pressed = 1;
            } else {
                pid_state.x = last_x;
                pid_state.y = last_y;
                pid_state.Pressed = 0;
            }
        }

        pid_state.Layer = 0;
        GUI_TOUCH_StoreStateEx(&pid_state);
    }
}

/*********************************************************************
*
* GUI_X_Init()
*
* Note:
* GUI_X_Init() is called from GUI_Init is a possibility to init
* some hardware which needs to be up and running before the GUI.
* If not required, leave this routine blank.
*/

void GUI_X_Init(void)
{
    gui_touch_fd = ms_io_open("/dev/touch0", O_RDONLY, 0666);
    if (gui_touch_fd < 0) {
        ms_printf("Failed to open /dev/touch0 device!\n");
        abort();
    }
}

/*********************************************************************
*
* GUI_X_ExecIdle
*
* Note:
* Called if WM is in idle state
*/

void GUI_X_ExecIdle(void)
{
    (void)ms_thread_sleep_ms(1);
}

/*********************************************************************
*
* Multitasking:
*
* GUI_X_InitOS()
* GUI_X_GetTaskId()
* GUI_X_Lock()
* GUI_X_Unlock()
*
* Note:
* The following routines are required only if emWin is used in a
* true multi task environment, which means you have more than one
* thread using the emWin API.
* In this case the
* #define GUI_OS 1
* needs to be in GUIConf.h
*/

/* Init OS */
void GUI_X_InitOS(void)
{
    (void)ms_mutex_create("emwin_lock", MS_WAIT_TYPE_PRIO, &gui_lock_handle);
    (void)ms_semb_create("emwin_semb", MS_FALSE, MS_WAIT_TYPE_PRIO, &gui_sem_handle);
}

void GUI_X_Unlock(void)
{
    (void)ms_mutex_unlock(gui_lock_handle);
}

void GUI_X_Lock(void)
{
    (void)ms_mutex_lock(gui_lock_handle, MS_TIMEOUT_FOREVER);
}

/* Get Task handle */
U32 GUI_X_GetTaskId(void)
{
    return ((U32)ms_thread_self());
}

void GUI_X_WaitEvent(void)
{
    (void)ms_semb_wait(gui_sem_handle, MS_TIMEOUT_FOREVER) ;
}

void GUI_X_SignalEvent(void)
{
    (void)ms_semb_post(gui_sem_handle);
}

/*********************************************************************
*
* Logging: OS dependent
*
* Note:
* Logging is used in higher debug levels only. The typical target
* build does not use logging and does therefor not require any of
* the logging routines below. For a release build without logging
* the routines below may be eliminated to save some space.
* (If the linker is not function aware and eliminates unreferenced
* functions automatically)
*
*/

void GUI_X_Log(const char *s)
{
    ms_puts(s);
}

void GUI_X_Warn(const char *s)
{
    ms_puts(s);
}

void GUI_X_ErrorOut(const char *s)
{
    ms_puts(s);
}

/*********************************************************************
*
*       GUI_X_Config
*
* Purpose:
*   Called during the initialization process in order to set up the
*   available memory for the GUI.
*/
void GUI_X_Config(void) {
    //
    // 32 bit aligned memory area
    //
    U32 * mem = (U32 *)ms_malloc(GUI_NUMBYTES);

    if (mem == MS_NULL) {
        ms_printf("Failed to allocate memory!\n");
        abort();
    }

    //
    // Assign memory to emWin
    //
    GUI_ALLOC_AssignMemory(mem, GUI_NUMBYTES);
    //
    // Set default font
    //
    GUI_SetDefaultFont(GUI_FONT_6X8);
}

/*********************************************************************
*
*       LCD_X_Config
*
* Purpose:
*   Called during the initialization process in order to set up the
*   display driver configuration.
*
*/
void LCD_X_Config(void)
{
    int fb_fd;
    ms_fb_var_screeninfo_t var_info;
    ms_fb_fix_screeninfo_t fix_info;
    const GUI_DEVICE_API *dev_api;
    const LCD_API_COLOR_CONV *color_conv_api;

    //
    // At first initialize use of multiple buffers on demand
    //
#if (NUM_BUFFERS > 1)
    GUI_MULTIBUF_Config(NUM_BUFFERS);
#endif

    fb_fd = ms_io_open("/dev/fb0", O_RDWR, 0666);
    if (fb_fd < 0) {
        ms_printf("Failed to open /dev/fb0 device!\n");
        abort();
        return;
    }

    if (ms_io_ioctl(fb_fd, MS_FB_CMD_GET_VSCREENINFO, &var_info) < 0) {
        ms_printf("Failed to get /dev/fb0 variable screen info!\n");
        abort();
    }

    if (ms_io_ioctl(fb_fd, MS_FB_CMD_GET_FSCREENINFO, &fix_info) < 0) {
        ms_printf("Failed to get /dev/fb0 fix screen info!\n");
        abort();
    }

    switch (var_info.bits_per_pixel) {
    case 1:
        dev_api = GUIDRV_LIN_1;
        color_conv_api = GUICC_1;
        break;

    case 2:
        dev_api = GUIDRV_LIN_2;
        color_conv_api = GUICC_2;
        break;

    case 4:
        dev_api = GUIDRV_LIN_4;
        color_conv_api = GUICC_4;
        break;

    case 8:
        dev_api = GUIDRV_LIN_8;
        color_conv_api = GUICC_8;
        break;

    case 16:
        dev_api = GUIDRV_LIN_16;
        color_conv_api = GUICC_M565;
        break;

    case 24:
        dev_api = GUIDRV_LIN_24;
        color_conv_api = GUICC_M888;
        break;

    case 32:
        dev_api = GUIDRV_LIN_32;
        color_conv_api = GUICC_M8888;
        break;

    default:
        ms_printf("No supported screen format!\n");
        abort();
        return;
    }

    //
    // Set display driver and color conversion for 1st layer
    //
    GUI_DEVICE_CreateAndLink(dev_api, color_conv_api, 0, 0);

    //
    // Display driver configuration, required for Lin-driver
    //
    if (LCD_GetSwapXY()) {
        LCD_SetSizeEx (0, var_info.yres, var_info.xres);
        LCD_SetVSizeEx(0, var_info.yres_virtual, var_info.xres_virtual);
    } else {
        LCD_SetSizeEx (0, var_info.xres, var_info.yres);
        LCD_SetVSizeEx(0, var_info.xres_virtual, var_info.yres_virtual);
    }

    LCD_SetVRAMAddrEx(0, (void *)fix_info.smem_start);

    //
    // Set user palette data (only required if no fixed palette is used)
    //
#if defined(PALETTE)
    LCD_SetLUTEx(0, PALETTE);
#endif
}

/*********************************************************************
*
*       LCD_X_DisplayDriver
*
* Purpose:
*   This function is called by the display driver for several purposes.
*   To support the according task the routine needs to be adapted to
*   the display controller. Please note that the commands marked with
*   'optional' are not cogently required and should only be adapted if
*   the display controller supports these features.
*
* Parameter:
*   LayerIndex - Index of layer to be configured
*   Cmd        - Please refer to the details in the switch statement below
*   pData      - Pointer to a LCD_X_DATA structure
*
* Return Value:
*   < -1 - Error
*     -1 - Command not handled
*      0 - Ok
*/
int LCD_X_DisplayDriver(unsigned LayerIndex, unsigned Cmd, void * pData)
{
    int r;

    switch (Cmd) {
    case LCD_X_INITCONTROLLER: {
        //
        // Called during the initialization process in order to set up the
        // display controller and put it into operation. If the display
        // controller is not initialized by any external routine this needs
        // to be adapted by the customer...
        //
        // ...
        return 0;
    }

    case LCD_X_SETVRAMADDR: {
        //
        // Required for setting the address of the video RAM for drivers
        // with memory mapped video RAM which is passed in the 'pVRAM' element of p
        //
        LCD_X_SETVRAMADDR_INFO * __unused p;
        p = (LCD_X_SETVRAMADDR_INFO *)pData;
        //...
        return 0;
    }

    case LCD_X_SETORG: {
        //
        // Required for setting the display origin which is passed in the 'xPos' and 'yPos' element of p
        //
        LCD_X_SETORG_INFO * __unused p;
        p = (LCD_X_SETORG_INFO *)pData;
        //...
        return 0;
    }

    case LCD_X_SHOWBUFFER: {
        //
        // Required if multiple buffers are used. The 'Index' element of p contains the buffer index.
        //
        LCD_X_SHOWBUFFER_INFO * __unused p;
        p = (LCD_X_SHOWBUFFER_INFO *)pData;
        //...
        return 0;
    }

    case LCD_X_SETLUTENTRY: {
        //
        // Required for setting a lookup table entry which is passed in the 'Pos' and 'Color' element of p
        //
        LCD_X_SETLUTENTRY_INFO * __unused p;
        p = (LCD_X_SETLUTENTRY_INFO *)pData;
        //...
        return 0;
    }

    case LCD_X_ON: {
        //
        // Required if the display controller should support switching on and off
        //
        return 0;
    }

    case LCD_X_OFF: {
        //
        // Required if the display controller should support switching on and off
        //
        // ...
        return 0;
    }

    default:
        r = -1;
    }

    return r;
}
