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

#ifndef RADIOPROTOCOL_H_
#define RADIOPROTOCOL_H_

#include "stdint.h"

/***** Enums *****/

typedef enum PacketType_ {
  Beacon        = 0,
  SensorData    = 1,
  SensorAck     = 2,
} PacketType;

typedef enum BeaconType_ {
  BeaconAdv = 0,
  BeaconReq = 1,
  BeaconRsp = 2,
  BeaconAck = 3,
} BeaconType;

/***** Typedefs *****/

/* Generic packet header structure */
typedef struct {
  uint8_t           destAddr;
  uint8_t           srcAddr;
  PacketType        packetType;
} PacketHeader_t;

/* Sensor data structure */
typedef struct {
    uint8_t simpleNodeData;
} SensorData_t;

/* Beacon packet structure */
typedef struct {
    /* Header */
    PacketHeader_t      header;
    BeaconType          beaconType;
    /* Connection event information */
    uint32_t            connectionTimeSlot;        /* Only contains a valid value if the packet is a beacon responds */
    uint32_t            connectionWindow;
} BeaconPacket_t;

/* Sensor packet structure */
typedef struct {
    /* Header */
    PacketHeader_t      header;
    /* Payload */
    SensorData_t        payload;
} SensorPacket_t;

/* Sensor ACK packet */
typedef struct {
    /* Header */
    PacketHeader_t    header;
} SensorAckPacket_t;

#endif /* RADIOPROTOCOL_H_ */
