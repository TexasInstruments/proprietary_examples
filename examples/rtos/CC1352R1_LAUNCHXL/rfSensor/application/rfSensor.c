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
 *  ======== rfSensor.c ========
 */

/* STD includes */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

/* SYS/BIOS includes */
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>

/* Drivers */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/rf/RF.h>
#include <ti/drivers/utils/Random.h>
#include <ti/display/Display.h>

/* Board Header file */
#include "ti_drivers_config.h"

/* RF settings */
#include "ti_radio_config.h"

/* RF queue and protocol */
#include "RFQueue.h"
#include "RadioProtocol.h"

/***** Defines *****/

#define SENSOR_ADDRESS                      (0xD3)
#define SENSOR_ORPHAN_THRESHOLD             (5)     /* Orphan timeout in number of missed packets */
#define RX_TIMEOUT                          (50)    /* RX Timeout in ms */

/* Packet RX Configuration */
#define DATA_ENTRY_HEADER_SIZE 8                    /* Constant header size of a Generic Data Entry */
#define MAX_LENGTH             255                  /* Max length byte the radio will accept */
#define NUM_DATA_ENTRIES       1                    /* Only one data entry is needed */
#define NUM_APPENDED_BYTES     1                    /* The Data Entries data field will contain:
                                                     * 1 Header byte (RF_cmdPropRx.rxConf.bIncludeHdr = 0x1)
                                                     */

/* Number of times the CS command should run when the channel is BUSY */
#define CS_RETRIES_WHEN_BUSY    10
/* The channel is reported BUSY if the RSSI is above this threshold */
#define RSSI_THRESHOLD_DBM      -80
#define IDLE_TIME_US            5000

/***** Enums ******/

/* Collector status enum typedefs */
typedef enum {
    BEACON_CONNECTED_ESTABLISHED    =  1,
    BEACON_FAILED_TO_JOIN           = -1,
} beaconStatus_t;

typedef enum {
    SENSOR_DATA_SENT_SUCCESSFULLY   =  1,
    SENSOR_DATA_NO_ACK              = -1,
} sensorStatus_t;

/***** Typedefs ******/

typedef struct {
    uint8_t     collectorAddress;           /* Address to the collector */
    uint8_t     isConnected;                /* Is connected to a collector */
    uint8_t     isOrphan;                   /* Is orphaned from a previous connection */
    uint8_t     missingCollectorAcks;       /* Total number of ACKs missed from the collector */
    uint8_t     orphanCounter;              /* Number of missed sensor ACKs until considering itself orphan */
    uint32_t    connectionTimeSlot;         /* Given time slot during connection */
    uint32_t    connectionWindow;           /* Connection time window */
    uint32_t    nextRadioTimeSlot;          /* Next radio time slot for sending sensor data */
} nodeConnectionInformation_t;

/***** Variable declarations *****/

/* RF driver handle */
static RF_Object rfObject;
static RF_Handle rfHandle;

/* RX Queue object that the RF Core will fill with data */
static dataQueue_t rxQueue;
static rfc_dataEntryGeneral_t* currentDataEntry;

/* Buffer which contains all Data Entries for receiving data.
 * Pragmas are needed to make sure this buffer is 4 byte aligned (requirement from the RF Core) */
#if defined(__TI_COMPILER_VERSION__)
#pragma DATA_ALIGN (rxBuffer, 4);
static uint8_t
rxBuffer[RF_QUEUE_DATA_ENTRY_BUFFER_SIZE(NUM_DATA_ENTRIES, MAX_LENGTH, NUM_APPENDED_BYTES)];
#elif defined(__IAR_SYSTEMS_ICC__)
#pragma data_alignment = 4
static uint8_t
rxBuffer[RF_QUEUE_DATA_ENTRY_BUFFER_SIZE(NUM_DATA_ENTRIES, MAX_LENGTH, NUM_APPENDED_BYTES)];

#elif defined(__GNUC__)
static uint8_t
rxBuffer[RF_QUEUE_DATA_ENTRY_BUFFER_SIZE(NUM_DATA_ENTRIES, MAX_LENGTH, NUM_APPENDED_BYTES)]
                                                              __attribute__((aligned(4)));
#else
#error This compiler is not supported.
#endif

/* Node data */
static volatile uint8_t nodeData = 0;

/* Node connection information  */
static volatile nodeConnectionInformation_t connectionInfo = {0};

/***** Prototypes *****/

uint8_t         listenForBeacon();
beaconStatus_t  sendJoinRequest(uint8_t collectorAddress);
sensorStatus_t  sendSensorData();
void            getReceivedPacket(uint8_t* buffer, uint16_t maxSize, uint8_t* length);

/***** Function definitions *****/

/*
 *  ======== processingThread ========
 */
void processingThread(uintptr_t arg0, uintptr_t arg1) {
    Display_Handle displayHandle = NULL;
    uint8_t i = 0;

    /* Open the Display handle */
    displayHandle = Display_open(Display_Type_UART, NULL);
    if (NULL == displayHandle)
    {
        /* Error opening the Display driver */
        while(1) {}
    }

    Display_printf(displayHandle, i++, 0, "rfSensor example - Node status");
    Display_printf(displayHandle, i++, 0, "==============================================================================");
    Display_printf(displayHandle, i++, 0, "|   Node   |   Value   |   Missed ACKs    |   Is connected   |   Is orphan   |");
    Display_printf(displayHandle, i++, 0, "==============================================================================");

    while(1)
    {
        Display_printf(displayHandle, i, 0,
            "|   0x%02x   |   %3d     |      %3d         |        %1d         |       %1d       |",
            SENSOR_ADDRESS,
            nodeData,
            connectionInfo.missingCollectorAcks,
            connectionInfo.isConnected,
            connectionInfo.isOrphan);

        /* Heartbeat LED is always nice */
        GPIO_toggle(CONFIG_GPIO_RLED);

        /* Update node data */
        nodeData++;

        /* Use an update interval of 2 seconds */
        Task_sleep((2 * 1000 * 1000) / Clock_tickPeriod);
    }
}

/*
 *  ======== mainThread ========
 */
void radioThread(uintptr_t arg0, uintptr_t arg1)
{
    RF_Params   rfParams;
    uint8_t     beaconSourceAddress;

    Random_seedAutomatic();

    /* Request access to the radio */
    RF_Params_init(&rfParams);
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup, &rfParams);

    /* Set the frequency */
    RF_postCmd(rfHandle, (RF_Op*)&RF_cmdFs, RF_PriorityNormal, NULL, 0);

    /* Prepare beacon RX queue */
    if (RFQueue_defineQueue(&rxQueue, rxBuffer, sizeof(rxBuffer), NUM_DATA_ENTRIES, MAX_LENGTH + NUM_APPENDED_BYTES))
    {
        /* Failed to allocate space for all data entries */
        while(1) {}
    }

    /* Setup cmdPropTx values that differs from the code export  */
    RF_cmdPropTx.startTrigger.triggerType   = TRIG_ABSTIME;
    RF_cmdPropTx.startTrigger.pastTrig      = 0;
    RF_cmdPropTx.startTime                  = 0;
    RF_cmdPropTx.condition.rule             = 0x0,

    /* Setup RF_cmdNop values that differs from the code export */
    RF_cmdNop.startTrigger.triggerType      = TRIG_ABSTIME;
    RF_cmdNop.startTrigger.pastTrig         = 1;

    /* Setup RF_cmdPropCs values that differs from the code export.
       Run next command if this command returned TRUE,
       skip a number of commands (.condition.nSkip - 1) if it returned FALSE
       End causes for the CMD_PROP_CS command:
       Observed channel state Busy with csConf.busyOp = 1:                            PROP_DONE_BUSY        TRUE
       0bserved channel state Idle with csConf.idleOp = 1:                            PROP_DONE_IDLE        FALSE
       Timeout trigger observed with channel state Busy:                              PROP_DONE_BUSY        TRUE
       Timeout trigger observed with channel state Idle:                              PROP_DONE_IDLE        FALSE
       Timeout trigger observed with channel state Invalid and csConf.timeoutRes = 0: PROP_DONE_BUSYTIMEOUT TRUE
       Timeout trigger observed with channel state Invalid and csConf.timeoutRes = 1: PROP_DONE_IDLETIMEOUT FALSE
       Received CMD_STOP after command started:                                       PROP_DONE_STOPPED     FALSE */
    RF_cmdPropCs.condition.rule             = COND_SKIP_ON_FALSE;
    RF_cmdPropCs.condition.nSkip            = 0x2;
    RF_cmdPropCs.csConf.busyOp              = 0x1;                      /* End carrier sense on channel Busy */
    RF_cmdPropCs.csEndTrigger.triggerType   = TRIG_REL_START;
    RF_cmdPropCs.csEndTime                  = (IDLE_TIME_US + 150) * 4; /* Add some margin */
    RF_cmdPropCs.csConf.bEnaRssi            = 0x1,                      /* Enable RSSI as a criterion */
    RF_cmdPropCs.rssiThr                    = RSSI_THRESHOLD_DBM;

    /* Setup RF_cmdPropCs values that differs from the code export.
       Run next command if this command returned TRUE, stop if it returned FALSE
       End causes for the CMD_COUNT_BRANCH command:
       Finished operation with counter = 0 when being started: DONE_OK         TRUE
       Finished operation with counter > 0 after decrementing: DONE_OK         TRUE
       Finished operation with counter = 0 after decrementing: DONE_COUNTDOWN  FALSE */
    RF_cmdCountBranch.condition.rule        = COND_STOP_ON_FALSE;

    /* Setup cmdPropRx values that differs from the code export  */
    RF_cmdPropRx.pQueue                     = &rxQueue;         /* Set the Data Entity queue for received data */
    RF_cmdPropRx.rxConf.bAutoFlushIgnored   = 1;                /* Discard ignored packets from RX queue */
    RF_cmdPropRx.rxConf.bAutoFlushCrcErr    = 1;                /* Discard packets with CRC error from RX queue */
    RF_cmdPropRx.maxPktLen                  = MAX_LENGTH;       /* Implement packet length filtering to avoid PROP_ERROR_RXBUF */
    RF_cmdPropRx.pktConf.bRepeatNok         = 1;
    RF_cmdPropRx.endTrigger.triggerType     = TRIG_REL_START;   /* End is relative to start */
    RF_cmdPropRx.endTrigger.pastTrig        = 1;                /* End past trigger is allowed */
    RF_cmdPropRx.pktConf.bChkAddress        = 1;

    /* Set up the next pointers for the LBT command chain */
    RF_cmdNop.pNextOp = (rfc_radioOp_t*)&RF_cmdPropCs;
    RF_cmdPropCs.pNextOp = (rfc_radioOp_t*)&RF_cmdCountBranch;
    RF_cmdCountBranch.pNextOp = (rfc_radioOp_t*)&RF_cmdPropTx;
    RF_cmdCountBranch.pNextOpIfOk = (rfc_radioOp_t*)&RF_cmdPropCs;

    while (1)
    {
        /* If not currently connected, look for a collector beacon */
        while(!connectionInfo.isConnected)
        {
            beaconSourceAddress = listenForBeacon();

            if (beaconSourceAddress > 0)
            {
                beaconStatus_t retVal = sendJoinRequest(beaconSourceAddress);

                /* Do actions based on return value */
                switch (retVal)
                {
                case BEACON_CONNECTED_ESTABLISHED:
                case BEACON_FAILED_TO_JOIN:
                default:
                    /* Do nothing */
                    break;
                }
            }
        }

        /* Send sensor data */
        if (connectionInfo.isConnected)
        {
            sensorStatus_t retVal = sendSensorData();

            /* Do actions based on return value */
            switch (retVal)
            {
            case SENSOR_DATA_SENT_SUCCESSFULLY:
            case SENSOR_DATA_NO_ACK:
            default:
                /* Do nothing */
                break;
            }
        }

        /* Sleep until just before next sensor packet,
         * wake up 2 ms early to have time to prepare the packet. */
        int64_t wakeUp = connectionInfo.nextRadioTimeSlot - RF_getCurrentTime();
        /* If we wrapped, change sign */
        if (wakeUp < 0)
        {
            wakeUp = (0xFFFFFFFF) & (wakeUp + 0xFFFFFFFF);
        }

        usleep(wakeUp - 2000);
    }
}

/*
 *  ======== sendBeacon ========
 *  Send data to the collector
 */
sensorStatus_t sendSensorData()
{
    RF_ScheduleCmdParams    schParams;
    SensorPacket_t          packet;
    uint8_t                 packetLength;
    sensorStatus_t          returnValue  = SENSOR_DATA_NO_ACK;
    SensorAckPacket_t       packetACK;

    /* Initialize the scheduling parameters */
    RF_ScheduleCmdParams_init(&schParams);

    /* Populate packet */
    packet.header.destAddr          = SENSOR_ADDRESS;
    packet.header.srcAddr           = connectionInfo.collectorAddress;
    packet.header.packetType        = SensorData;
    packet.payload.simpleNodeData   = nodeData;

    /* Setup TX for sensor packet */
    RF_cmdPropTx.startTrigger.triggerType   = TRIG_ABSTIME;                     /* Trigger on absolute time */
    RF_cmdPropTx.pPkt                       = (uint8_t *) &packet;              /* Packet to send */
    RF_cmdPropTx.pktLen                     = sizeof(packet);
    RF_cmdPropTx.pNextOp                    = (RF_Op*) &RF_cmdPropRx;           /* RX operation is chained to the TX operation */
    RF_cmdPropTx.condition.rule             = 0;
    RF_cmdPropTx.startTime                  = connectionInfo.nextRadioTimeSlot; /* Schedule at next radio time slot */

    /* Setup RX to receive packet ACK */
    RF_cmdPropRx.address0                   = SENSOR_ADDRESS;                       /* Filter on own address */
    RF_cmdPropRx.address1                   = SENSOR_ADDRESS;
    RF_cmdPropRx.startTrigger.triggerType   = TRIG_NOW;                             /* Trigger immediately */
    RF_cmdPropRx.endTime                    = RF_convertMsToRatTicks(RX_TIMEOUT);   /* Set RX timeout to RX_TIMEOUT */
    RF_cmdPropRx.pNextOp                    = NULL;                                 /* There is no chained command */
    RF_cmdPropRx.condition.rule             = 1;

    /* Schedule the sensor packet transmission (chained TX + RX) */
    RF_CmdHandle cmdHandle = RF_scheduleCmd(rfHandle,
        (RF_Op*)&RF_cmdPropTx,
        &schParams,
        NULL,
        RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled | RF_EventCmdAborted | RF_EventCmdStopped
    );

    /* Pend for command to complete */
    RF_EventMask terminationReason = RF_pendCmd(rfHandle,
        cmdHandle,
        RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled | RF_EventCmdAborted | RF_EventCmdStopped
    );

    /* If RX finished successfully with a received payload */
    if (terminationReason & (RF_EventRxEntryDone | RF_EventRxOk))
    {
        /* Read out the data from the RX queue */
        getReceivedPacket((uint8_t*) &packetACK, sizeof(packetACK), &packetLength);

        /* Is the received packet an beacon request? */
        if (packetACK.header.packetType == SensorAck)
        {
            /* Sent successful and ACKed */
            returnValue = SENSOR_DATA_SENT_SUCCESSFULLY;
        }
        else
        {
            /* Did not receive the expected ACK */
            returnValue = SENSOR_DATA_NO_ACK;
        }
    }
    else
    {
        /* Did not receive the expected ACK */
        returnValue = SENSOR_DATA_NO_ACK;
    }

    if (returnValue == SENSOR_DATA_NO_ACK)
    {
        /* Count statistics here */
        connectionInfo.missingCollectorAcks++;
        connectionInfo.orphanCounter++;

        /* Have we reached the orphan threshold? */
        if (connectionInfo.orphanCounter == SENSOR_ORPHAN_THRESHOLD)
        {
            connectionInfo.isOrphan    = 1;
            connectionInfo.isConnected = 0;
        }

    }

    connectionInfo.nextRadioTimeSlot += RF_convertMsToRatTicks(connectionInfo.connectionWindow);
    return returnValue;
}

/*
 *  ======== sendJoinRequest ========
 *  Send request to join a broadcasting collector.
 */
beaconStatus_t sendJoinRequest(uint8_t collectorAddress)
{
    RF_ScheduleCmdParams    schParams;
    RF_CmdHandle            cmdHandle;
    RF_EventMask            terminationReason;
    BeaconPacket_t          packet;
    uint8_t                 packetLength;
    uint8_t                 sendDelay;
    beaconStatus_t          returnValue = BEACON_FAILED_TO_JOIN;

    /* Initialize the scheduling parameters */
    RF_ScheduleCmdParams_init(&schParams);

    /* Populate packet */
    packet.header.destAddr      = collectorAddress;
    packet.header.srcAddr       = SENSOR_ADDRESS;
    packet.header.packetType    = Beacon;
    packet.beaconType           = BeaconReq;

    /* Setup TX for join request, include LBT functionality */
    RF_cmdPropTx.startTrigger.triggerType   = TRIG_NOW;                     /* Trigger on absolute time */
    RF_cmdPropTx.pPkt                       = (uint8_t *) &packet;          /* Packet to send */
    RF_cmdPropTx.pktLen                     = sizeof(packet);
    RF_cmdPropTx.pNextOp                    = (RF_Op*)&RF_cmdPropRx;        /* RX operation is chained to the TX operation */
    RF_cmdPropTx.condition.rule             = 0;

    /* Clean previous statuses if any */
    RF_cmdNop.status = IDLE;
    RF_cmdPropCs.status = IDLE;
    RF_cmdCountBranch.status = IDLE;
    RF_cmdPropTx.status = IDLE;
    RF_cmdCountBranch.counter = CS_RETRIES_WHEN_BUSY;

    /* Setup RX for join responds */
    RF_cmdPropRx.address0                   = SENSOR_ADDRESS;                     /* Filter on own address */
    RF_cmdPropRx.address1                   = SENSOR_ADDRESS;
    RF_cmdPropRx.startTrigger.triggerType   = TRIG_NOW;                           /* Trigger immediately */
    RF_cmdPropRx.endTime                    = RF_convertMsToRatTicks(RX_TIMEOUT); /* Set RX timeout to 50 ms */
    RF_cmdPropRx.pNextOp                    = NULL;                               /* There is no chained command */
    RF_cmdPropRx.condition.rule             = 1;

    /* Random delay between [1, 20] ms */
    Random_getBytes(&sendDelay, 1);
    sendDelay = sendDelay % 20 + 1;

    /* Send the join request as soon as possible using absolute time.
     * To make sure the start time of the command, when processed, is not in
     * the past, we set the start to be some random delay ms into the future.*/
    RF_cmdNop.startTime =  RF_getCurrentTime() + RF_convertMsToRatTicks(sendDelay);

    /* Send join request (chained TX + RX) */
    cmdHandle = RF_scheduleCmd(rfHandle,
        (RF_Op*)&RF_cmdNop,
        &schParams,
        NULL,
        RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled | RF_EventCmdAborted | RF_EventCmdStopped
    );

    /* Pend for command to complete */
    terminationReason = RF_pendCmd(rfHandle,
        cmdHandle,
        RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled | RF_EventCmdAborted | RF_EventCmdStopped
    );

    /* If RX finished successfully with a received payload */
    if (terminationReason & (RF_EventRxEntryDone | RF_EventRxOk))
    {
        /* Read out the data from the RX queue */
        getReceivedPacket((uint8_t*) &packet, sizeof(packet), &packetLength);

        /* Is the received packet an beacon request? */
        if ((packet.header.packetType == Beacon) &&
            (packet.beaconType == BeaconRsp))
        {

            /* Send an ACK back with same information*/
            packet.header.destAddr   = SENSOR_ADDRESS;
            packet.header.srcAddr    = connectionInfo.collectorAddress;
            packet.header.packetType = Beacon;
            packet.beaconType        = BeaconAck;

            /* This TX has no chained command */
            RF_cmdPropTx.pNextOp        = NULL;
            RF_cmdPropTx.condition.rule = 0,

            /* ACK the join responds as soon as possible using absolute time
             * To make sure the start time of the command, when processed, is
             * not in the past, we set the start to be 1 ms into the future. */
            RF_cmdPropTx.startTime =  RF_getCurrentTime() + RF_convertMsToRatTicks(1);

            /* Send responds ACK */
            RF_CmdHandle cmdHandle = RF_scheduleCmd(rfHandle,
                (RF_Op*)&RF_cmdPropTx,
                &schParams,
                NULL,
                RF_EventLastCmdDone | RF_EventCmdCancelled | RF_EventCmdAborted | RF_EventCmdStopped
            );

            /* Pend for command to complete */
            RF_EventMask terminationReason = RF_pendCmd(rfHandle,
                cmdHandle,
                RF_EventLastCmdDone | RF_EventCmdCancelled | RF_EventCmdAborted  | RF_EventCmdStopped
            );

            /* Check to see if ACK was sent successfully */
            if (terminationReason & RF_EventLastCmdDone)
            {
                /* Update connection information */
                connectionInfo.isOrphan           = 0;
                connectionInfo.isConnected        = 1;
                connectionInfo.orphanCounter      = 0;
                connectionInfo.collectorAddress   = collectorAddress;
                connectionInfo.connectionTimeSlot = packet.connectionTimeSlot;
                connectionInfo.connectionWindow   = packet.connectionWindow;

                /* Calculate next sensor radio time based on previous beacon advertisement */
                connectionInfo.nextRadioTimeSlot += RF_convertMsToRatTicks(connectionInfo.connectionTimeSlot);

                returnValue = BEACON_CONNECTED_ESTABLISHED;
            }
            else
            {
                returnValue = BEACON_FAILED_TO_JOIN;
            }
        }
    }

    return returnValue;
}

/*
 *  ======== listenForBeacon ========
 *  Listen for a beacon from a collector.
 */
uint8_t listenForBeacon()
{
    RF_ScheduleCmdParams    schParams;
    rfc_propRxOutput_t      rxStatistics;
    BeaconPacket_t          packet;
    uint8_t                 returnValue = 0;
    uint8_t                 packetLength;

    /* Initialize the scheduling parameters */
    RF_ScheduleCmdParams_init(&schParams);

    /* Setup RX to listen for beacon */
    RF_cmdPropRx.address0                   = 0xFF;                         /* Filter on broadcast messages only */
    RF_cmdPropRx.address1                   = 0xFF;
    RF_cmdPropRx.startTrigger.triggerType   = TRIG_NOW;                     /* Trigger now */
    RF_cmdPropRx.endTime                    = RF_convertMsToRatTicks(4000); /* Set RX Timeout to 4 s.
                                                                             * Enough to catch a beacon as we
                                                                             * know the beacon interval is 2 s. */
    RF_cmdPropRx.endTrigger.triggerType     = TRIG_REL_START;               /* End is relative to start */
    RF_cmdPropRx.endTrigger.pastTrig        = 1;                            /* End past trigger is allowed */
    RF_cmdPropRx.pOutput                    = (uint8_t*)&rxStatistics;      /* Pointer to RX statistics */

    /* Schedule RX command */
    RF_CmdHandle cmdHandle = RF_scheduleCmd(rfHandle,
        (RF_Op*)&RF_cmdPropRx,
        &schParams,
        NULL,
        RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled | RF_EventCmdAborted | RF_EventCmdStopped
    );

    /* Pend for command to complete */
    RF_EventMask terminationReason = RF_pendCmd(rfHandle,
        cmdHandle,
        RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled | RF_EventCmdAborted  | RF_EventCmdStopped
    );

    /* If RX finished successfully with a received payload */
    if (terminationReason & (RF_EventRxEntryDone | RF_EventRxOk))
    {
        /* Read out the data from the RX queue */
        getReceivedPacket((uint8_t*) &packet, sizeof(packet), &packetLength);

        if ((packet.header.packetType == Beacon) && (packet.beaconType == BeaconAdv))
        {
            /* Store the connection window */
            connectionInfo.connectionWindow = packet.connectionWindow;
            /* Record beacon time stamp to schedule future commands if able to join the collector */
            connectionInfo.nextRadioTimeSlot = rxStatistics.timeStamp;

            /* Return address to the collector */
            returnValue = packet.header.srcAddr;
        }
   }

    /* Before returning, set the pOutput buffer to NULL as it
     * is not longer used. This is important as the statistics
     * structure pointed to is in the local scope and located on
     * the stack. As we exit the function, the pointer is no longer
     * valid.*/
    RF_cmdPropRx.pOutput = NULL;

    return returnValue;
}

/*
 *  ======== getReceivedPacket ========
 *  Reads out the first available packet from the radio RX queue
 */
void getReceivedPacket(uint8_t* buffer, uint16_t maxSize, uint8_t* length)
{
    uint32_t packetLength;
    uint8_t* packetDataPointer;

    /* Get current data entry */
    currentDataEntry = RFQueue_getDataEntry();

    /* Handle the packet data, located at &currentDataEntry->data:
     * - Length is the first byte with the current configuration
     * - Data starts from the second byte */
    packetLength      = *(uint8_t*)(&currentDataEntry->data);
    packetDataPointer = (uint8_t*)(&currentDataEntry->data + 1);

    if (packetLength > maxSize)
    {
        packetLength = maxSize;
    }

    /* If there is any data in the packet ... */
    if (packetLength > 0)
    {
        /* ... copy it into the provided buffer */
        memcpy(buffer, packetDataPointer, packetLength);
        *length = packetLength;

        /* Clean up the queue element */
        memset(&currentDataEntry->data, 0, packetLength);

        /* Move to next queue element */
        RFQueue_nextEntry();
    }
}

