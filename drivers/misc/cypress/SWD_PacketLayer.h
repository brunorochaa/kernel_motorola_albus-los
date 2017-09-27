/******************************************************************************
* File Name: SWD_PacketLayer.h
* Version 1.0
*
* Description:
*  This header file contains the constant definitions, function declarations
*  associated with the packet layer of the SWD protocol.
*
* Note:
*
*******************************************************************************
* Copyright (2013), Cypress Semiconductor Corporation.
*******************************************************************************
* This software is owned by Cypress Semiconductor Corporation (Cypress) and is
* protected by and subject to worldwide patent protection (United States and
* foreign), United States copyright laws and international treaty provisions.
* Cypress hereby grants to licensee a personal, non-exclusive, non-transferable
* license to copy, use, modify, create derivative works of, and compile the
* Cypress Source Code and derivative works for the sole purpose of creating
* custom software in support of licensee product to be used only in conjunction
* with a Cypress integrated circuit as specified in the applicable agreement.
* Any reproduction, modification, translation, compilation, or representation of
* this software except as specified above is prohibited without the express
* written permission of Cypress.
*
* Disclaimer: CYPRESS MAKES NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, WITH
* REGARD TO THIS MATERIAL, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
* Cypress reserves the right to make changes without further notice to the
* materials described herein. Cypress does not assume any liability arising out
* of the application or use of any product or circuit described herein. Cypress
* does not authorize its products for use as critical components in life-support
* systems where a malfunction or failure may reasonably be expected to result in
* significant injury to the user. The inclusion of Cypress' product in a life-
* support systems application implies that the manufacturer assumes all risk of
* such use and in doing so indemnifies Cypress against all charges. Use may be
* limited by and subject to the applicable Cypress software license agreement.
******************************************************************************/

#ifndef __SWD_PACKETLAYER_H
#define __SWD_PACKETLAYER_H

/******************************************************************************
*   Constant definitions
******************************************************************************/

/* Data size of a SWD Packet in bytes */
#define DATA_BYTES_PER_PACKET   4

/* Number of dummy SWDCK clocks at end of each SWD packet.
   Required for bit banging programmers where clock is not free running  */
#define NUMBER_OF_DUMMY_SWD_CLOCK_CYCLES    3

/* Minimum number of SWDCK clock cycles to reset the SWD line state */
#define NUMBER_OF_SWD_RESET_CLOCK_CYCLES    51

/* ACK response in a SWD packet is a 3-bit value */
#define NUMBER_OF_ACK_BITS  3

/* SWD ACK response meanings.
*  Parity error definition is not part of SWD ACK response from target PSoC 4.
*  The actual ACK response is only 3-bit length. The parity error bit is
*  defined as fourth bit by host application. It is generated by host if there
*  is a parity error in the SWD Read packet data from target PSoC 4 */
#define SWD_OK_ACK          0x01
#define SWD_WAIT_ACK        0x02
#define SWD_FAULT_ACK       0x04
#define SWD_PARITY_ERROR    0x08

/* Maximum SWD packet loop timeout for SWD_WAIT_ACK response */
#define NUMBER_OF_WAIT_ACK_LOOPS    5

/* Mask value to define Most significant bit (MSb) in a Byte */
#define MSB_BIT_MASK		0x80
/* Mask value to define Least significant bit (LSb) in a Byte */
#define LSB_BIT_MASK		0x01

/******************************************************************************
*   Global Variable declaration
******************************************************************************/
/* The below global variables are accessed by the upper layer files to create
   SWD packet header, ACK, data */

extern unsigned char swd_PacketHeader;
extern unsigned char swd_PacketAck;
extern unsigned char swd_PacketData[DATA_BYTES_PER_PACKET];

/******************************************************************************
*   Function Prototypes
******************************************************************************/
/* The below public fuctions are called by the upper layer files to send SWD
   packets */
void Swd_WritePacket(void);
void Swd_ReadPacket(void);
void Swd_LineReset(void);
void Swd_SendDummyClocks(void);

#endif				/* __SWD_PACKETLAYER_H */

/* [] END OF FILE */
