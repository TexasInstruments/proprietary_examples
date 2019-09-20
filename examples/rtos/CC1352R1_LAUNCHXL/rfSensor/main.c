/*
 * Copyright (c) 2018, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,

 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== main.c ========
 */
#include <stdint.h>

/* RTOS header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>

#include <ti/drivers/GPIO.h>

/* Example/Board Header files */
#include "ti_drivers_config.h"

void radioThread(uintptr_t arg0, uintptr_t arg1);
void processingThread(uintptr_t arg0, uintptr_t arg1);

#define RADIO_TASK_PRI     2
#define RADIO_TASK_SIZE    1024
static uint8_t radioTaskStack[RADIO_TASK_SIZE];
static Task_Struct radioTaskStruct;
static Task_Handle radioTaskHandle;

#define NODE_TASK_PRI     1
#define NODE_TASK_SIZE    1024
static uint8_t nodeTaskStack[NODE_TASK_SIZE];
static Task_Struct nodeTaskStruct;
static Task_Handle nodeTaskHandle;

/*
 *  ======== rfSensor_createRadioTask ========
 */
Task_Handle* rfSensor_createRadioTask(void)
{
    Task_Params taskParams;
    Task_Params_init(&taskParams);
    taskParams.stack = radioTaskStack;
    taskParams.stackSize = RADIO_TASK_SIZE;
    taskParams.priority = RADIO_TASK_PRI;

    Task_construct(&radioTaskStruct, radioThread, &taskParams, NULL);
    radioTaskHandle = Task_handle(&radioTaskStruct);
    return &radioTaskHandle;
}

/*
 *  ======== rfSensor_createNodeTask ========
 */
Task_Handle* rfSensor_createNodeTask(void)
{
    Task_Params taskParams;
    Task_Params_init(&taskParams);
    taskParams.stack = nodeTaskStack;
    taskParams.stackSize = NODE_TASK_SIZE;
    taskParams.priority = NODE_TASK_PRI;

    Task_construct(&nodeTaskStruct, processingThread, &taskParams, NULL);
    nodeTaskHandle = Task_handle(&nodeTaskStruct);
    return &nodeTaskHandle;
}

/*
 *  ======== main ========
 */
int main(void)
{
    Board_init();

    GPIO_init();

    rfSensor_createRadioTask();
    rfSensor_createNodeTask();

    BIOS_start();

    return (0);
}
