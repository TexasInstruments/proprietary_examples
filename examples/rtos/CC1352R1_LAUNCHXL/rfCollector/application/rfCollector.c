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
 *  ======== rfCollector.c ========
 */

/* STD includes */
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

/* Drivers */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/rf/RF.h>
#include <ti/display/Display.h>
#include <ti/drivers/utils/List.h>

/* Board Header file */
#include "ti_drivers_config.h"

/* RF settings */
#include "ti_radio_config.h"

/* RF queue and protocol */
#include "RFQueue.h"
#include "RadioProtocol.h"

/***** Defines *****/
#define COLLECTOR_ADDRESS           (0xC0)
#define SENSOR_ORPHAN_TIMEOUT       (5)        /* Orphan timeout in number of missed packets */
#define BEACON_INTERVAL             (2000)     /* Beacon interval in ms */
#define TIMESLOT_SIZE               (200)      /* Size of a timeslot in ms*/
#define RX_TIMEOUT                  (50)       /* RX Timeout in ms */

/* Maximum numbers of supported nodes depend on the timeslot size and beacon interval */
#define MAX_NODE_ENTRIES            ((BEACON_INTERVAL / TIMESLOT_SIZE) - 1)

/* Packet RX Configuration */
#define DATA_ENTRY_HEADER_SIZE      (8)        /* Constant header size of a Generic Data Entry */
#define MAX_LENGTH                  (255)      /* Max length byte the radio will accept */
#define NUM_DATA_ENTRIES            (1)        /* Only one data entry is needed */
#define NUM_APPENDED_BYTES          (1)        /* The Data Entries data field will contain:
                                                * 1 Header byte (RF_cmdPropRx.rxConf.bIncludeHdr = 0x1)
                                                */
/***** Enums ******/

/* Collector status enum typedefs */
typedef enum {
    BEACON_ACK_OK           =  1,
    BEACON_NO_ACK           = -1,
    BEACON_NO_FREE_SLOTS    = -2,
} beaconStatus_t;

typedef enum {
    SENSOR_DATA_OK           =  1,
    SENSOR_DATA_NOT_RECEIVED = -1,
    SENSOR_DATA_NOT_ACKED    = -2,
} sensorStatus_t;

/***** Typedefs *****/

/* Node element structure */
typedef struct {
    List_Elem   elem;
    uint8_t     nodeAddress;
    uint32_t    connectionTimeSlot;
    uint32_t    nextRadioTimeSlot;
    uint32_t    missedPackets;
    uint8_t     orphanCounter;
    uint8_t     isOrphan;
    /* Store the last Node data */
    SensorData_t lastSensorData;
} NodeElement_t;

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

/* List containing the joined nodes */
static List_List nodeList;

/* Number of entries in the nodeStructure */
static volatile uint8_t nodeEntries = 0;

/***** Prototypes *****/

uint8_t         sendBeacon(uint32_t delayInRatTicks);
beaconStatus_t  processJoinRequest(uint8_t nodeAddress, uint32_t nextBeaconTime);
sensorStatus_t  listenForNode(NodeElement_t* node);
void            getReceivedPacket(uint8_t* buffer, uint16_t maxSize, uint8_t* length);

/***** Function definitions *****/

/*
 *  ======== processingThread ========
 */
void *processingThread(void *arg0) {
    Display_Handle  displayHandle;

    /* Open the display handle */
    displayHandle = Display_open(Display_Type_UART, NULL);
    if (NULL == displayHandle) {
        /* Error opening the Display driver */
        while(1) {};
    }

    Display_printf(displayHandle, 0, 0, "rfCollector example - Node status");
    Display_printf(displayHandle, 1, 0, "===============================================================");
    Display_printf(displayHandle, 2, 0, "|   Node   |   Value   |    Missed packets    |   Is orphan   |");
    Display_printf(displayHandle, 3, 0, "===============================================================");

    while(1) {
        /* For each node */
        NodeElement_t *temp;
        uint8_t i = 0;
        /* Give the node a slot if it is not an orphan */
        for (temp = (NodeElement_t *) List_head(&nodeList); temp != NULL; temp = (NodeElement_t *) List_next((List_Elem *) temp)) {
            Display_printf(displayHandle, i + 4, 0,
                "|   0x%02x   |   %3d     |         %3d          |       %1d       |",
                temp->nodeAddress,
                temp->lastSensorData.simpleNodeData,
                temp->missedPackets,
                temp->isOrphan);
            i++;
        }

        /* Heart beat LED is always nice */
        GPIO_toggle(CONFIG_GPIO_RLED);

        /* Sleep for BEACON_INTERVAL seconds */
        usleep(BEACON_INTERVAL * 1000);
    }
}

/*
 *  ======== radioThread ========
 */
void *radioThread(void *arg0) {
    RF_Params   rfParams;
    uint8_t     nodeAddress     = 0;
    uint32_t    nextBeaconTime  = 0;

    /* Initialize the node list */
    List_clearList(&nodeList);

    /* Request access to the radio */
    RF_Params_init(&rfParams);
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup, &rfParams);

    /* Set the frequency */
    RF_runCmd(rfHandle, (RF_Op*)&RF_cmdFs, RF_PriorityNormal, NULL, 0);

    /* Prepare RX queue */
    if( RFQueue_defineQueue(&rxQueue, rxBuffer, sizeof(rxBuffer), NUM_DATA_ENTRIES, MAX_LENGTH + NUM_APPENDED_BYTES)) {
        /* Failed to allocate space for all data entries */
        while(1) {};
    }

    /* Setup cmdPropTx values that differs from the code export  */
    RF_cmdPropTx.startTrigger.triggerType   = TRIG_ABSTIME;
    RF_cmdPropTx.startTrigger.pastTrig      = 0;
    RF_cmdPropTx.startTime                  = 0;
    RF_cmdPropTx.condition.rule             = 0x0,

    /* Setup cmdPropRx values that differs from the code export  */
    RF_cmdPropRx.pQueue                     = &rxQueue;                             /* Set the Data Entity queue for received data */
    RF_cmdPropRx.rxConf.bAutoFlushIgnored   = 1;                                    /* Discard ignored packets from Rx queue */
    RF_cmdPropRx.rxConf.bAutoFlushCrcErr    = 1;                                    /* Discard packets with CRC error from Rx queue */
    RF_cmdPropRx.maxPktLen                  = MAX_LENGTH;                           /* Implement packet length filtering to avoid PROP_ERROR_RXBUF */
    RF_cmdPropRx.endTime                    = RF_convertMsToRatTicks(RX_TIMEOUT);   /* Set RX timeout to RX_TIMEOUT ms */
    RF_cmdPropRx.endTrigger.triggerType     = TRIG_REL_START;                       /* End time is relative to start */
    RF_cmdPropRx.endTrigger.pastTrig        = 1;                                    /* End past trigger is allowed */
    RF_cmdPropRx.pktConf.bRepeatNok         = 1;
    RF_cmdPropRx.pktConf.bChkAddress        = 1;
    RF_cmdPropRx.rxConf.bAppendStatus       = 0;                                    /* Do not need status byte appended */

    /* Schedule the first beacon one BEACON_INTERVAL into the future */
    nextBeaconTime = RF_getCurrentTime() + RF_convertMsToRatTicks(BEACON_INTERVAL);

    while (1) {
        /* Send a beacon to probe for new nodes */
        nodeAddress = sendBeacon(nextBeaconTime);

        /* If a node request to join, process it */
        if (nodeAddress > 0) {
            beaconStatus_t retVal = processJoinRequest(nodeAddress, nextBeaconTime);

            /* Do actions based on return value */
            switch (retVal) {
            case BEACON_ACK_OK:
            case BEACON_NO_ACK:
            case BEACON_NO_FREE_SLOTS:
            default:
                /* Do nothing */
                break;
            }
        }

        /* If there is any joined nodes, process them */
        if (nodeEntries) {
            /* Process each node */
            NodeElement_t* node;
            for (node = (NodeElement_t *) List_head(&nodeList);
                 node != NULL;
                 node = (NodeElement_t *) List_next((List_Elem *) node)) {
                if (!(node->isOrphan)) {
                    sensorStatus_t retVal = listenForNode(node);

                    /* Do actions based on return value */
                    switch (retVal) {
                    case SENSOR_DATA_OK:
                    case SENSOR_DATA_NOT_ACKED:
                    case SENSOR_DATA_NOT_RECEIVED:
                    default:
                        /* Do nothing */
                        break;
                    }
                }
            }
        }

        /* Calculate the next beacon time stamp */
        nextBeaconTime = nextBeaconTime + RF_convertMsToRatTicks(BEACON_INTERVAL);
    }
}

/*
 *  ======== listenForNode ========
 *  Listen for a valid sensor data packet from the node indexed by nodeIndex
 */
sensorStatus_t listenForNode(NodeElement_t* node) {
   RF_ScheduleCmdParams schParams;
   RF_CmdHandle         cmdHandle;
   RF_EventMask         terminationReason;
   SensorPacket_t       packet;
   SensorAckPacket_t    packetACK;
   sensorStatus_t       returnValue = SENSOR_DATA_NOT_RECEIVED;
   uint8_t              packetLength;

   /* Initialize the scheduling parameters */
   RF_ScheduleCmdParams_init(&schParams);

   /* Prepare TX to send an ACK following the sensor data */
   RF_cmdPropTx.startTrigger.triggerType    = TRIG_ABSTIME;             /* Trigger on absolute time */
   RF_cmdPropTx.pPkt                        = (uint8_t *) &packetACK;   /* Packet to send */
   RF_cmdPropTx.pktLen                      = sizeof(packetACK);
   RF_cmdPropTx.pNextOp                     = NULL;                     /* There is no chained command */
   RF_cmdPropTx.condition.rule              = 1;
   RF_cmdPropTx.startTrigger.pastTrig       = 0;                        /* Past triggering is not allowed */

   /* Setup RX to listen for the node */
   RF_cmdPropRx.address0                    = node->nodeAddress;        /* Filter on sensor address */
   RF_cmdPropRx.address1                    = node->nodeAddress;
   RF_cmdPropRx.startTrigger.triggerType    = TRIG_ABSTIME;             /* Trigger on absolute time */
   RF_cmdPropRx.startTrigger.pastTrig       = 0;                        /* Past triggering is not allowed */
   RF_cmdPropRx.startTime                   = node->nextRadioTimeSlot;  /* Wake up to listen for the node */
   RF_cmdPropRx.pNextOp                     = NULL;                     /* There is no chained command */
   RF_cmdPropRx.condition.rule              = 1;

   /* Schedule RX command */
   cmdHandle = RF_scheduleCmd(rfHandle, (RF_Op*)&RF_cmdPropRx, &schParams, NULL,
                              RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled |
                              RF_EventCmdAborted  | RF_EventCmdStopped);

   /* Pend for command to complete */
   terminationReason = RF_pendCmd(rfHandle, cmdHandle,
                                  RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled |
                                  RF_EventCmdAborted  | RF_EventCmdStopped);

   /* If RX finished successfully with a received payload */
   if (terminationReason & (RF_EventRxEntryDone | RF_EventRxOk)) {
       /* Read out the data from the RX queue */
       getReceivedPacket((uint8_t*) &packet, sizeof(packet), &packetLength);

       /* If it is of the expected packet type */
       if (packet.header.packetType == SensorData) {
           /* Populate the ACK packet */
           packetACK.header.destAddr    = node->nodeAddress;
           packetACK.header.srcAddr     = COLLECTOR_ADDRESS;
           packetACK.header.packetType  = SensorAck;

           /* Send the ACK as soon as possible using absolute time.
            * To make sure the start time of the command, when processed, is
            * not in the past, we set the start to be 1 ms into the future. */
           RF_cmdPropTx.startTime = RF_getCurrentTime() + RF_convertMsToRatTicks(1);

           /* Send sensor data ACK */
           cmdHandle = RF_scheduleCmd(rfHandle, (RF_Op*)&RF_cmdPropTx, &schParams, NULL, RF_EventLastCmdDone);

           /* Pend for command to complete */
           terminationReason = RF_pendCmd(rfHandle, cmdHandle, RF_EventLastCmdDone);

           /* If command was successful */
           if (terminationReason & RF_EventLastCmdDone) {
               /* Restart orphan timeout counter */
               node->orphanCounter = 0;
               node->isOrphan      = 0;

               node->lastSensorData.simpleNodeData = packet.payload.simpleNodeData;

               /* Return success */
               returnValue = SENSOR_DATA_OK;
           }
           else {
               /* Return data not ACKed */
               returnValue = SENSOR_DATA_NOT_ACKED;
           }
       }
       else {
           /* Unexpected sensor packet */
           returnValue = SENSOR_DATA_NOT_RECEIVED;
       }
   }
   else {
           /* Unexpected sensor packet */
           returnValue = SENSOR_DATA_NOT_RECEIVED;
   }

   if ((returnValue == SENSOR_DATA_NOT_ACKED) ||
       (returnValue == SENSOR_DATA_NOT_RECEIVED)) {
       /* We did not hear from the node when expected */
       node->missedPackets++;
       node->orphanCounter++;

       if (node->orphanCounter == SENSOR_ORPHAN_TIMEOUT) {
           node->isOrphan = 1;
       }
   }

   /* Update the nodes nextRadioTimeslot to be one
    * BEACON_INTERVAL into the future */
   node->nextRadioTimeSlot += RF_convertMsToRatTicks(BEACON_INTERVAL);

   return returnValue;
}

/*
 *  ======== processJoinRequest ========
 *  Process node join request.
 */
beaconStatus_t processJoinRequest(uint8_t nodeAddress, uint32_t nextBeaconTime) {
    RF_ScheduleCmdParams    schParams;
    BeaconPacket_t          packet;
    uint8_t                 packetLength;
    uint8_t                 orphanNode = 0;
    beaconStatus_t          returnValue = BEACON_NO_ACK;
    uint32_t                nextFreeTimeslot = 0;
    NodeElement_t           *temp;

    /* Initialize the scheduling parameters */
    RF_ScheduleCmdParams_init(&schParams);

    /* Check if the sensor is an orphan or if it is a new sensor */
    for (temp = (NodeElement_t *) List_head(&nodeList);
         temp != NULL;
         temp = (NodeElement_t *) List_next((List_Elem *) temp)) {
        /* Is it a new or orphan node? */
        if (temp->nodeAddress == nodeAddress) {
            /* Orphan node */
            orphanNode = nodeAddress;
            break;
        }
    }

    if (!orphanNode) {
        /* If the node was not a orphan node  */
        if (nodeEntries > MAX_NODE_ENTRIES) {
            /* The collector can not support more nodes */
            returnValue = BEACON_NO_FREE_SLOTS;
        }
    }

    /* Setup a beacon responds packet */
    packet.header.destAddr     = nodeAddress;
    packet.header.srcAddr      = COLLECTOR_ADDRESS;
    packet.header.packetType   = Beacon;
    packet.beaconType          = BeaconRsp;
    packet.connectionWindow    = BEACON_INTERVAL;

    /* Update packet with the connection time slot */
    if (!orphanNode) {
        /* Get the next free timeslot */
        if (true == List_empty(&nodeList)) {
            nextFreeTimeslot = TIMESLOT_SIZE;
        }
        else {
            temp = (NodeElement_t *) List_tail(&nodeList);
            nextFreeTimeslot = temp->connectionTimeSlot + TIMESLOT_SIZE;
        }
        packet.connectionTimeSlot = nextFreeTimeslot;
    }
    else {
        packet.connectionTimeSlot = temp->connectionTimeSlot;
    }

    /* Setup TX to send the beacon responds packet */
    RF_cmdPropTx.startTrigger.triggerType   = TRIG_ABSTIME;             /* Trigger on absolute time */
    RF_cmdPropTx.pPkt                       = (uint8_t *) &packet;      /* Packet to send */
    RF_cmdPropTx.pktLen                     = sizeof(packet);
    RF_cmdPropTx.pNextOp                    = (RF_Op*)&RF_cmdPropRx;    /* RX operation is chained to the TX operation */
    RF_cmdPropTx.condition.rule             = 0x0,

    /* Setup RX to receive the beacon responds ACK */
    RF_cmdPropRx.address0                   = nodeAddress;  /* Filter on own address */
    RF_cmdPropRx.address1                   = nodeAddress;
    RF_cmdPropRx.startTrigger.triggerType   = TRIG_NOW;     /* Trigger immediately */
    RF_cmdPropRx.pNextOp                    = NULL;         /* There is no chained command */
    RF_cmdPropRx.condition.rule             = 1,

    /* Send the packet as soon as possible using absolute time.
     * To make sure the start time of the command, when processed, is
     * not in the past, we set the start to be 1 ms into the future. */
    RF_cmdPropTx.startTime = RF_getCurrentTime() + RF_convertMsToRatTicks(1);

    /* Send node join responds (chained TX + RX) */
    RF_CmdHandle cmdHandle = RF_scheduleCmd(rfHandle, (RF_Op*)&RF_cmdPropTx,
                                                       &schParams, NULL, RF_EventRxEntryDone | RF_EventRxOk |
                                                       RF_EventCmdCancelled | RF_EventCmdAborted  | RF_EventCmdStopped);

    /* Pend for command to complete */
    RF_EventMask terminationReason = RF_pendCmd(rfHandle, cmdHandle, RF_EventRxEntryDone | RF_EventRxOk |
                                                RF_EventCmdCancelled | RF_EventCmdAborted  | RF_EventCmdStopped);

    /* If RX finished successfully with a received payload */
    if (terminationReason & (RF_EventRxEntryDone | RF_EventRxOk)) {

        /* Read out the data from the RX queue */
        getReceivedPacket((uint8_t*) &packet, sizeof(packet), &packetLength);

        /* Is the received packet an beacon ACK? */
        if ((packet.header.packetType == Beacon) &&
            (packet.beaconType == BeaconAck)) {
                /* If a new node, add to node list */
                if (!orphanNode) {
                    nodeEntries++;
                    temp = (NodeElement_t*) malloc(sizeof(NodeElement_t));

                    if (NULL != temp) {
                        temp->nodeAddress           = nodeAddress;
                        temp->connectionTimeSlot    = nextFreeTimeslot;
                        temp->nextRadioTimeSlot     = 0;
                        temp->missedPackets         = 0;
                        temp->orphanCounter         = 0;
                        temp->isOrphan              = 0;

                        List_put(&nodeList, (List_Elem *) temp);
                    }
                }
                else {
                    /* Reset orphan timeout counter and status*/
                    temp->orphanCounter = 0;
                    temp->isOrphan      = 0;
                }

                /* Set the initial node time stamp based on the previous beacon time stamp */
                 temp->nextRadioTimeSlot = nextBeaconTime +
                                           RF_convertMsToRatTicks(temp->connectionTimeSlot);

                /* Return address of the requesting node */
                returnValue = BEACON_ACK_OK;
        }
        else {
            /* We did not get the expected ACK, remove from structure */
            returnValue = BEACON_NO_ACK;
        }
    }

    return returnValue;
}

/*
 *  ======== sendBeacon ========
 *  Schedule a beacon and listen for join requests
 */
uint8_t sendBeacon(uint32_t delayInRatTicks) {
    RF_ScheduleCmdParams    schParams;
    RF_CmdHandle            cmdHandle;
    RF_EventMask            terminationReason ;
    BeaconPacket_t          packet;
    uint8_t                 returnValue = 0;
    uint8_t                 packetLength;

    /* Initialize the scheduling parameters */
    RF_ScheduleCmdParams_init(&schParams);

    /* Populate the beacon advertisement packet */
    packet.header.destAddr      = 0xFF;                 /* Directed to everyone */
    packet.header.srcAddr       = COLLECTOR_ADDRESS;   /* Address back to the collector */
    packet.header.packetType    = Beacon;               /* Is a beacon packet */
    packet.beaconType           = BeaconAdv;            /* Is beacon advertisement */
    packet.connectionWindow     = BEACON_INTERVAL;      /* Collector connection window, same as beacon interval */

    /* Setup TX for beacon */
    RF_cmdPropTx.startTrigger.triggerType   = TRIG_ABSTIME;             /* Trigger on absolute time */
    RF_cmdPropTx.pPkt                       = (uint8_t *) &packet;      /* Packet to send */
    RF_cmdPropTx.pktLen                     = sizeof(packet);
    RF_cmdPropTx.pNextOp                    = (RF_Op*)&RF_cmdPropRx;    /* RX operation is chained to the TX operation */
    RF_cmdPropTx.condition.rule             = 0,

    /* Setup RX to receive a possible beacon request from a listening node */
    RF_cmdPropRx.address0                   = COLLECTOR_ADDRESS;   /* Filter on own address */
    RF_cmdPropRx.address1                   = COLLECTOR_ADDRESS;
    RF_cmdPropRx.startTrigger.triggerType   = TRIG_NOW;             /* Trigger immediately */
    RF_cmdPropRx.pNextOp                    = NULL;                 /* There is no chained command */
    RF_cmdPropRx.condition.rule             = 1,

    /* Schedule the TX command delayInRatTicks number of RAT ticks into the future */
    RF_cmdPropTx.startTime = delayInRatTicks;

    /* Send join beacon (chained TX + RX) */
    cmdHandle = RF_scheduleCmd(rfHandle, (RF_Op*)&RF_cmdPropTx, &schParams, NULL,
                               RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled |
                               RF_EventCmdAborted  | RF_EventCmdStopped);

    /* Pend for the commands to complete */
    terminationReason = RF_pendCmd(rfHandle, cmdHandle,
                                   RF_EventRxEntryDone | RF_EventRxOk | RF_EventCmdCancelled |
                                   RF_EventCmdAborted | RF_EventCmdStopped);

    /* If RX finished successfully with a received payload */
    if (terminationReason & (RF_EventRxEntryDone | RF_EventRxOk)) {

        /* Read out the data from the RX queue */
        getReceivedPacket((uint8_t*) &packet, sizeof(packet), &packetLength);

        /* Is the received packet a beacon request? */
        if ((packet.header.packetType == Beacon) &&
            (packet.beaconType == BeaconReq)) {

            /* Return address of the requesting node */
            returnValue = packet.header.srcAddr;
        }
    }

    return returnValue;
}

/*
 *  ======== getReceivedPacket ========
 *  Reads out the first available packet from the radio RX queue
 */
void getReceivedPacket(uint8_t* buffer, uint16_t maxSize, uint8_t* length) {
    uint32_t packetLength;
    uint8_t* packetDataPointer;

    /* Get current data entry */
    currentDataEntry = RFQueue_getDataEntry();

    /* Handle the packet data, located at &currentDataEntry->data:
     * - Length is the first byte with the current configuration
     * - Data starts from the second byte */
    packetLength      = *(uint8_t*)(&currentDataEntry->data);
    packetDataPointer = (uint8_t*)(&currentDataEntry->data + 1);

    if(packetLength > maxSize){
        packetLength = maxSize;
    }

    /* If there is any data in the packet ... */
    if (packetLength > 0) {
        /* ... copy it into the provided buffer */
        memcpy(buffer, packetDataPointer, packetLength);
        *length = packetLength;

        /* Clean up the queue element */
        memset(&currentDataEntry->data, 0, packetLength);

        /* Move to next queue element */
        RFQueue_nextEntry();
    }
}
