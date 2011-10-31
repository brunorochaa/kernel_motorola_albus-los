/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include "as102_drv.h"
#include "as10x_types.h"
#include "as10x_cmd.h"

/***************************/
/* FUNCTION DEFINITION     */
/***************************/

/**
   \brief  send get context command to AS10x
   \param  phandle:   pointer to AS10x handle
   \param  tag:       context tag
   \param  pvalue:    pointer where to store context value read
   \return 0 when no error, < 0 in case of error.
   \callgraph
*/
int as10x_cmd_get_context(as10x_handle_t *phandle, uint16_t tag,
			  uint32_t *pvalue)
{
	int  error;
	struct as10x_cmd_t *pcmd, *prsp;

	ENTER();

	pcmd = phandle->cmd;
	prsp = phandle->rsp;

	/* prepare command */
	as10x_cmd_build(pcmd, (++phandle->cmd_xid),
			sizeof(pcmd->body.context.req));

	/* fill command */
	pcmd->body.context.req.proc_id = cpu_to_le16(CONTROL_PROC_CONTEXT);
	pcmd->body.context.req.tag = cpu_to_le16(tag);
	pcmd->body.context.req.type = cpu_to_le16(GET_CONTEXT_DATA);

	/* send command */
	if (phandle->ops->xfer_cmd) {
		error  = phandle->ops->xfer_cmd(phandle,
						(uint8_t *) pcmd,
						sizeof(pcmd->body.context.req)
						+ HEADER_SIZE,
						(uint8_t *) prsp,
						sizeof(prsp->body.context.rsp)
						+ HEADER_SIZE);
	} else {
		error = AS10X_CMD_ERROR;
	}

	if (error < 0)
		goto out;

	/* parse response: context command do not follow the common response */
	/* structure -> specific handling response parse required            */
	error = as10x_context_rsp_parse(prsp, CONTROL_PROC_CONTEXT_RSP);

	if (error == 0) {
		/* Response OK -> get response data */
		*pvalue = le32_to_cpu(prsp->body.context.rsp.reg_val.u.value32);
		/* value returned is always a 32-bit value */
	}

out:
	LEAVE();
	return error;
}

/**
   \brief  send set context command to AS10x
   \param  phandle:   pointer to AS10x handle
   \param  tag:       context tag
   \param  value:     value to set in context
   \return 0 when no error, < 0 in case of error.
   \callgraph
*/
int as10x_cmd_set_context(as10x_handle_t *phandle, uint16_t tag,
			  uint32_t value)
{
	int error;
	struct as10x_cmd_t *pcmd, *prsp;

	ENTER();

	pcmd = phandle->cmd;
	prsp = phandle->rsp;

	/* prepare command */
	as10x_cmd_build(pcmd, (++phandle->cmd_xid),
			sizeof(pcmd->body.context.req));

	/* fill command */
	pcmd->body.context.req.proc_id = cpu_to_le16(CONTROL_PROC_CONTEXT);
	/* pcmd->body.context.req.reg_val.mode initialization is not required */
	pcmd->body.context.req.reg_val.u.value32 = cpu_to_le32(value);
	pcmd->body.context.req.tag = cpu_to_le16(tag);
	pcmd->body.context.req.type = cpu_to_le16(SET_CONTEXT_DATA);

	/* send command */
	if (phandle->ops->xfer_cmd) {
		error  = phandle->ops->xfer_cmd(phandle,
						(uint8_t *) pcmd,
						sizeof(pcmd->body.context.req)
						+ HEADER_SIZE,
						(uint8_t *) prsp,
						sizeof(prsp->body.context.rsp)
						+ HEADER_SIZE);
	} else {
		error = AS10X_CMD_ERROR;
	}

	if (error < 0)
		goto out;

	/* parse response: context command do not follow the common response */
	/* structure -> specific handling response parse required            */
	error = as10x_context_rsp_parse(prsp, CONTROL_PROC_CONTEXT_RSP);

out:
	LEAVE();
	return error;
}

/**
   \brief  send eLNA change mode command to AS10x
   \param  phandle:   pointer to AS10x handle
   \param  tag:       context tag
   \param  mode:      mode selected:
		     - ON    : 0x0 => eLNA always ON
		     - OFF   : 0x1 => eLNA always OFF
		     - AUTO  : 0x2 => eLNA follow hysteresis parameters to be
				      ON or OFF
   \return 0 when no error, < 0 in case of error.
   \callgraph
*/
int as10x_cmd_eLNA_change_mode(as10x_handle_t *phandle, uint8_t mode)
{
	int error;
	struct as10x_cmd_t *pcmd, *prsp;

	ENTER();

	pcmd = phandle->cmd;
	prsp = phandle->rsp;

	/* prepare command */
	as10x_cmd_build(pcmd, (++phandle->cmd_xid),
			sizeof(pcmd->body.cfg_change_mode.req));

	/* fill command */
	pcmd->body.cfg_change_mode.req.proc_id =
		cpu_to_le16(CONTROL_PROC_ELNA_CHANGE_MODE);
	pcmd->body.cfg_change_mode.req.mode = mode;

	/* send command */
	if (phandle->ops->xfer_cmd) {
		error  = phandle->ops->xfer_cmd(phandle, (uint8_t *) pcmd,
				sizeof(pcmd->body.cfg_change_mode.req)
				+ HEADER_SIZE, (uint8_t *) prsp,
				sizeof(prsp->body.cfg_change_mode.rsp)
				+ HEADER_SIZE);
	} else {
		error = AS10X_CMD_ERROR;
	}

	if (error < 0)
		goto out;

	/* parse response */
	error = as10x_rsp_parse(prsp, CONTROL_PROC_ELNA_CHANGE_MODE_RSP);

out:
	LEAVE();
	return error;
}

/**
   \brief  Parse context command response. Since this command does not follow
	   the common response, a specific parse function is required.
   \param  prsp:       pointer to AS10x command response buffer
   \param  proc_id:    id of the command
   \return 0 when no error, < 0 in case of error.
	   ABILIS_RC_NOK
   \callgraph
*/
int as10x_context_rsp_parse(struct as10x_cmd_t *prsp, uint16_t proc_id)
{
	int err;

	err = prsp->body.context.rsp.error;

	if ((err == 0) &&
	    (le16_to_cpu(prsp->body.context.rsp.proc_id) == proc_id)) {
		return 0;
	}
	return AS10X_CMD_ERROR;
}
