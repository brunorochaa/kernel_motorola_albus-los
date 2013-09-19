/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/* Machine-generated file; do not edit. */

#ifndef __ARCH_UART_DEF_H__
#define __ARCH_UART_DEF_H__
#define UART_DIVISOR 0x0158
#define UART_FIFO_COUNT 0x0110
#define UART_FLAG 0x0108
#define UART_INTERRUPT_MASK 0x0208
#define UART_INTERRUPT_MASK__RDAT_ERR_SHIFT 0
#define UART_INTERRUPT_MASK__RDAT_ERR_WIDTH 1
#define UART_INTERRUPT_MASK__RDAT_ERR_RESET_VAL 1
#define UART_INTERRUPT_MASK__RDAT_ERR_RMASK 0x1
#define UART_INTERRUPT_MASK__RDAT_ERR_MASK  0x1
#define UART_INTERRUPT_MASK__RDAT_ERR_FIELD 0,0
#define UART_INTERRUPT_MASK__WDAT_ERR_SHIFT 1
#define UART_INTERRUPT_MASK__WDAT_ERR_WIDTH 1
#define UART_INTERRUPT_MASK__WDAT_ERR_RESET_VAL 1
#define UART_INTERRUPT_MASK__WDAT_ERR_RMASK 0x1
#define UART_INTERRUPT_MASK__WDAT_ERR_MASK  0x2
#define UART_INTERRUPT_MASK__WDAT_ERR_FIELD 1,1
#define UART_INTERRUPT_MASK__FRAME_ERR_SHIFT 2
#define UART_INTERRUPT_MASK__FRAME_ERR_WIDTH 1
#define UART_INTERRUPT_MASK__FRAME_ERR_RESET_VAL 1
#define UART_INTERRUPT_MASK__FRAME_ERR_RMASK 0x1
#define UART_INTERRUPT_MASK__FRAME_ERR_MASK  0x4
#define UART_INTERRUPT_MASK__FRAME_ERR_FIELD 2,2
#define UART_INTERRUPT_MASK__PARITY_ERR_SHIFT 3
#define UART_INTERRUPT_MASK__PARITY_ERR_WIDTH 1
#define UART_INTERRUPT_MASK__PARITY_ERR_RESET_VAL 1
#define UART_INTERRUPT_MASK__PARITY_ERR_RMASK 0x1
#define UART_INTERRUPT_MASK__PARITY_ERR_MASK  0x8
#define UART_INTERRUPT_MASK__PARITY_ERR_FIELD 3,3
#define UART_INTERRUPT_MASK__RFIFO_OVERFLOW_SHIFT 4
#define UART_INTERRUPT_MASK__RFIFO_OVERFLOW_WIDTH 1
#define UART_INTERRUPT_MASK__RFIFO_OVERFLOW_RESET_VAL 1
#define UART_INTERRUPT_MASK__RFIFO_OVERFLOW_RMASK 0x1
#define UART_INTERRUPT_MASK__RFIFO_OVERFLOW_MASK  0x10
#define UART_INTERRUPT_MASK__RFIFO_OVERFLOW_FIELD 4,4
#define UART_INTERRUPT_MASK__RFIFO_AFULL_SHIFT 5
#define UART_INTERRUPT_MASK__RFIFO_AFULL_WIDTH 1
#define UART_INTERRUPT_MASK__RFIFO_AFULL_RESET_VAL 1
#define UART_INTERRUPT_MASK__RFIFO_AFULL_RMASK 0x1
#define UART_INTERRUPT_MASK__RFIFO_AFULL_MASK  0x20
#define UART_INTERRUPT_MASK__RFIFO_AFULL_FIELD 5,5
#define UART_INTERRUPT_MASK__TFIFO_RE_SHIFT 7
#define UART_INTERRUPT_MASK__TFIFO_RE_WIDTH 1
#define UART_INTERRUPT_MASK__TFIFO_RE_RESET_VAL 1
#define UART_INTERRUPT_MASK__TFIFO_RE_RMASK 0x1
#define UART_INTERRUPT_MASK__TFIFO_RE_MASK  0x80
#define UART_INTERRUPT_MASK__TFIFO_RE_FIELD 7,7
#define UART_INTERRUPT_MASK__RFIFO_WE_SHIFT 8
#define UART_INTERRUPT_MASK__RFIFO_WE_WIDTH 1
#define UART_INTERRUPT_MASK__RFIFO_WE_RESET_VAL 1
#define UART_INTERRUPT_MASK__RFIFO_WE_RMASK 0x1
#define UART_INTERRUPT_MASK__RFIFO_WE_MASK  0x100
#define UART_INTERRUPT_MASK__RFIFO_WE_FIELD 8,8
#define UART_INTERRUPT_MASK__WFIFO_RE_SHIFT 9
#define UART_INTERRUPT_MASK__WFIFO_RE_WIDTH 1
#define UART_INTERRUPT_MASK__WFIFO_RE_RESET_VAL 1
#define UART_INTERRUPT_MASK__WFIFO_RE_RMASK 0x1
#define UART_INTERRUPT_MASK__WFIFO_RE_MASK  0x200
#define UART_INTERRUPT_MASK__WFIFO_RE_FIELD 9,9
#define UART_INTERRUPT_MASK__RFIFO_ERR_SHIFT 10
#define UART_INTERRUPT_MASK__RFIFO_ERR_WIDTH 1
#define UART_INTERRUPT_MASK__RFIFO_ERR_RESET_VAL 1
#define UART_INTERRUPT_MASK__RFIFO_ERR_RMASK 0x1
#define UART_INTERRUPT_MASK__RFIFO_ERR_MASK  0x400
#define UART_INTERRUPT_MASK__RFIFO_ERR_FIELD 10,10
#define UART_INTERRUPT_MASK__TFIFO_AEMPTY_SHIFT 11
#define UART_INTERRUPT_MASK__TFIFO_AEMPTY_WIDTH 1
#define UART_INTERRUPT_MASK__TFIFO_AEMPTY_RESET_VAL 1
#define UART_INTERRUPT_MASK__TFIFO_AEMPTY_RMASK 0x1
#define UART_INTERRUPT_MASK__TFIFO_AEMPTY_MASK  0x800
#define UART_INTERRUPT_MASK__TFIFO_AEMPTY_FIELD 11,11
#define UART_INTERRUPT_STATUS 0x0200
#define UART_RECEIVE_DATA 0x0148
#define UART_TRANSMIT_DATA 0x0140
#define UART_TYPE 0x0160
#define UART_TYPE__SBITS_SHIFT 0
#define UART_TYPE__SBITS_WIDTH 1
#define UART_TYPE__SBITS_RESET_VAL 1
#define UART_TYPE__SBITS_RMASK 0x1
#define UART_TYPE__SBITS_MASK  0x1
#define UART_TYPE__SBITS_FIELD 0,0
#define UART_TYPE__SBITS_VAL_ONE_SBITS 0x0
#define UART_TYPE__SBITS_VAL_TWO_SBITS 0x1
#define UART_TYPE__DBITS_SHIFT 2
#define UART_TYPE__DBITS_WIDTH 1
#define UART_TYPE__DBITS_RESET_VAL 0
#define UART_TYPE__DBITS_RMASK 0x1
#define UART_TYPE__DBITS_MASK  0x4
#define UART_TYPE__DBITS_FIELD 2,2
#define UART_TYPE__DBITS_VAL_EIGHT_DBITS 0x0
#define UART_TYPE__DBITS_VAL_SEVEN_DBITS 0x1
#define UART_TYPE__PTYPE_SHIFT 4
#define UART_TYPE__PTYPE_WIDTH 3
#define UART_TYPE__PTYPE_RESET_VAL 3
#define UART_TYPE__PTYPE_RMASK 0x7
#define UART_TYPE__PTYPE_MASK  0x70
#define UART_TYPE__PTYPE_FIELD 4,6
#define UART_TYPE__PTYPE_VAL_NONE 0x0
#define UART_TYPE__PTYPE_VAL_MARK 0x1
#define UART_TYPE__PTYPE_VAL_SPACE 0x2
#define UART_TYPE__PTYPE_VAL_EVEN 0x3
#define UART_TYPE__PTYPE_VAL_ODD 0x4
#endif /* !defined(__ARCH_UART_DEF_H__) */
