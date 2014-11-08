// $Id: mbnet.c 1182 2011-04-21 21:18:08Z tk $
/*
 * MBNet Functions
 *
 * TODO: try recovery on CAN Bus errors
 * TODO: remove slaves from info list which timed out during transaction!
 *
 * ==========================================================================
 *
 *  Copyright (C) 2011 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>

#include <FreeRTOS.h>
#include <portmacro.h>

#include <app.h>

#include "mbnet.h"
#include "mbnet_hal.h"


/////////////////////////////////////////////////////////////////////////////
// for optional debugging messages via MIDI
/////////////////////////////////////////////////////////////////////////////
#define DEBUG_VERBOSE_LEVEL 1
#define DEBUG_MSG MIOS32_MIDI_SendDebugMessage


/////////////////////////////////////////////////////////////////////////////
// Local defines
/////////////////////////////////////////////////////////////////////////////

#define MBNET_TIMEOUT_CTR_MAX 5000


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

// error state flags
mbnet_state_t mbnet_state;

// my node ID
static u8 my_node_id = 0xff; // (node not configured)

// master only: search node
static u8 search_slave_id = 0xff; // no slave yet

// info index of available slave nodes
// if >128, used as retry counter! (to save memory)
static u8 slave_nodes_ix[MBNET_SLAVE_NODES_END-MBNET_SLAVE_NODES_BEGIN+1];

// informations about scanned nodes (contains "pong" reply)
static mbnet_msg_t slave_nodes_info[MBNET_SLAVE_NODES_MAX];

// for retry mechanism
static u8          last_req_slave_id = 0xff;
static mbnet_id_t  last_req_mbnet_id;
static mbnet_msg_t last_req_msg;
static u8          last_req_dlc;

// turns to 1 if scan for MBNet nodes is finished
static u8 scan_finished;

#if DEBUG_VERBOSE_LEVEL >= 2
// only for debugging: skip messages after more than 8 TOS=1/2
static u32 tos12_ctr = 0;
#endif


/////////////////////////////////////////////////////////////////////////////
// Local prototypes
/////////////////////////////////////////////////////////////////////////////
static s32 MBNET_BusErrorCheck(void);


/////////////////////////////////////////////////////////////////////////////
// Initializes CAN interface
// IN: <mode>: currently only mode 0 supported
// OUT: returns < 0 if initialisation failed
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_Init(u32 mode)
{
  // currently only mode 0 supported
  if( mode != 0 )
    return -1; // unsupported mode

  // clear state flags
  mbnet_state.ALL = 0;

  // set default node ID to 0xff (node not configured)
  // The application has to use MBNET_NodeIDSet() to initialise it, and to configure the CAN filters
  my_node_id = 0xff;

  // invalidate slave informations
  MBNET_Reconnect();

  // init hardware
  if( MBNET_HAL_Init(mode) < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[MBNET] ----- initialisation FAILED! ------------------------------------------\n");
#endif
    return -1;
  }

#if DEBUG_VERBOSE_LEVEL >= 2
  DEBUG_MSG("[MBNET] ----- initialized -----------------------------------------------------\n");
#endif

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Changes the Node ID and configures the CAN filters accordingly
// IN: node_id: configures the MBNet Node ID
//              if [3:0] == 0 (0x00, 0x10, 0x20, ... 0x70), the node can act
//              as a MBNet master and access MBNet slaves
// OUT: returns < 0 if configuration failed
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_NodeIDSet(u8 node_id)
{
  // my node_id must be 0..127
  if( node_id >= 128 )
    return -1;

  // take over node ID
  my_node_id = node_id;

  // CAN acceptance filter initialisation
  MBNET_HAL_FilterInit(node_id);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Returns the Node ID
// IN: -
// OUT: if -1: node not configured yet
//      if >= 0: node ID
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_NodeIDGet(void)
{
  return my_node_id == 0xff ? -1 : my_node_id;
}


/////////////////////////////////////////////////////////////////////////////
// Returns 1 if scan for MBNet nodes has been finished
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_ScanFinished(void)
{
  return scan_finished;
}


/////////////////////////////////////////////////////////////////////////////
// Returns informations about a found node
// IN: slave node ID (MBNET_SLAVE_NODES_BEGIN..MBNET_SLAVE_NODES_END)
// OUT: "pong" reply in *info
//      returns 0 if slave has been scanned
//      returns -1 if this node hasn't been configured yet
//      returns -2 if this node isn't configured as master
//      returns -3 if slave index outside allowed range
//      returns -4 if slave info not available (no slave found at this index)
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_SlaveNodeInfoGet(u8 slave_id, mbnet_msg_t **info)
{
  u8 ix;
  
  if( my_node_id >= 128 )
    return -1; // node not configured

  if( my_node_id & 0x0f )
    return -2; // no master

#if MBNET_SLAVE_NODES_BEGIN > 0
  if( slave_id < MBNET_SLAVE_NODES_BEGIN || slave_id > MBNET_SLAVE_NODES_END )
#else
  if( slave_id > MBNET_SLAVE_NODES_END )
#endif
    return -3; // outside allowed range

  if( (ix=slave_nodes_ix[slave_id-MBNET_SLAVE_NODES_BEGIN]) >= 128 )
    return -4; // slave info not available

  *info = (mbnet_msg_t *)&slave_nodes_info[ix];

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// invalidates all slave informations and scans the MBNET (again)
// OUT: < 0 on errora
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_Reconnect(void)
{
  int i;

  scan_finished = 0;

  // invalidate slave id to be searched
  search_slave_id = 0xff;

  // clear slave node index
  for(i=0; i<=MBNET_SLAVE_NODES_END-MBNET_SLAVE_NODES_BEGIN; ++i)
    slave_nodes_ix[i] = 0xff-MBNET_NODE_SCAN_RETRY; // invalid slave entry, set retry counter

  // clear slave info
  for(i=0; i<MBNET_SLAVE_NODES_MAX; ++i) {
    slave_nodes_info[i].data_l = 0;
    slave_nodes_info[i].data_h = 0;
  }

  return 0; // no error
}



/////////////////////////////////////////////////////////////////////////////
// Returns the MBNet error state:
// OUT: 0 if no errors
//      -1 if MBNet reached panic state (failing transactions, automatic retries to recover)
//      -2 if MBNet reached "permanent off" state (recovery not possible)
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_ErrorStateGet(void)
{
  if( mbnet_state.PERMANENT_OFF )
    return -2;
  if( mbnet_state.PANIC )
    return -1;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// internal function to send a MBNet message
// Used by MBNET_SendReq and MBNET_SendAck
// returns 1 if message sent successfully
// returns -3 on transmission error
/////////////////////////////////////////////////////////////////////////////
static s32 MBNET_SendMsg(mbnet_id_t mbnet_id, mbnet_msg_t msg, u8 dlc)
{
  s32 status;

  if( (status=MBNET_HAL_Send(mbnet_id, msg, dlc)) < 0 )
    return status;

#if DEBUG_VERBOSE_LEVEL >= 3
  if( tos12_ctr <= 8 ) {
    DEBUG_MSG("[MBNET] sent %s: ID:%02x TOS=%d DLC=%d MSG=%02x %02x %02x %02x %02x %02x %02x %02x\n",
	      mbnet_id.ack ? "ACK" : "REQ",
	      mbnet_id.node,
	      mbnet_id.tos,
	      dlc,
	      msg.bytes[0], msg.bytes[1], msg.bytes[2], msg.bytes[3],
	      msg.bytes[4], msg.bytes[5], msg.bytes[6], msg.bytes[7]);
  }
#endif

  return status;
}


/////////////////////////////////////////////////////////////////////////////
// Sends request to slave node
// IN: <slave_id>: slave node ID (0x00..0x7f)
//     <tos_req>: request TOS
//     <control>: 16bit control field of ID
//     <msg>: MBNet message (see mbnet_msg_t structure)
//     <dlc>: data field length (0..8)
// OUT: returns 1 if message sent successfully
//      returns -1 if this node hasn't been configured yet
//      returns -2 if this node isn't configured as master
//      returns -3 on transmission error
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_SendReq(u8 slave_id, mbnet_tos_req_t tos_req, u16 control, mbnet_msg_t msg, u8 dlc)
{
  if( my_node_id >= 128 )
    return -1; // node not configured

  if( my_node_id & 0x0f )
    return -2; // this node isn's configured as master

  mbnet_id_t mbnet_id;
  mbnet_id.control = control;
  mbnet_id.tos     = tos_req;
  mbnet_id.ms      = my_node_id >> 4;
  mbnet_id.ack     = 0;
  mbnet_id.node    = slave_id;

  // remember settings for MBNET_SendReqAgain()
  last_req_slave_id = slave_id;
  last_req_mbnet_id = mbnet_id;
  last_req_msg = msg;
  last_req_dlc = dlc;

  return MBNET_SendMsg(mbnet_id, msg, dlc);
}


/////////////////////////////////////////////////////////////////////////////
// If a slave acknowledged with retry, the last message has to be sent again
// It can be initiated with this function
// Only required if MBNET_WaitAck_NonBlocking() is used!
// Automatically handled by MBNET_WaitAck()
// IN: <slave_id>: slave node ID (0x00..0x7f)
// OUT: returns 1 if message sent successfully
//      returns -1 if this node hasn't been configured yet
//      returns -2 if this node isn't configured as master
//      returns -3 on transmission error
//      returns -7 if slave message lost (sequence error)
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_SendReqAgain(u8 slave_id)
{
  if( my_node_id >= 128 )
    return -1; // node not configured

  if( my_node_id & 0x0f )
    return -2; // this node isn's configured as master

  if( slave_id != last_req_slave_id )
    return -7; // sequence error

  return MBNET_SendMsg(last_req_mbnet_id, last_req_msg, last_req_dlc);
}


/////////////////////////////////////////////////////////////////////////////
// Sends acknowledge reply to master node
// IN: <master_id>:  master node ID (0x00, 0x10, 0x20, ... 0x70)
//     <tos_ack>: acknowledge TOS
//     <msg>: MBNet message (see mbnet_msg_t structure)
//     <dlc>: data field length (0..8)
// OUT: returns 0 if message sent successfully
//      returns -1 if this node hasn't been configured yet
//      returns -2 if destination node is not a master node
//      returns -3 on transmission error
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_SendAck(u8 master_id, mbnet_tos_ack_t tos_ack, mbnet_msg_t msg, u8 dlc)
{
  if( my_node_id >= 128 )
    return -1; // node not configured

  if( master_id & 0x0f )
    return -2; // destination node is not a master node

  mbnet_id_t mbnet_id;
  mbnet_id.control = my_node_id;
  mbnet_id.tos     = tos_ack;
  mbnet_id.ms      = 0;
  mbnet_id.ack     = 1;
  mbnet_id.node    = master_id;

  return MBNET_SendMsg(mbnet_id, msg, dlc);
}



/////////////////////////////////////////////////////////////////////////////
// Checks for acknowledge from a specific slave
// IN: <slave_id>: slave node ID (0x00..0x7f)
//     <*msg>: received MBNet message (see mbnet_msg_t structure)
//     <*dlc>: received data field length (0..8)
// OUT: returns 0 if message received successfully
//      returns -1 if this node hasn't been configured yet
//      returns -2 if this node isn't configured as master
//      returns -3 on transmission error
//      returns -4 if slave acknowledged with retry (send message again)
//      returns -5 if no response from slave yet (check again or timeout!)
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_WaitAck_NonBlocking(u8 slave_id, mbnet_msg_t *ack_msg, u8 *dlc)
{
  if( my_node_id >= 128 )
    return -1; // node not configured

  if( my_node_id & 0x0f )
    return -2; // this node isn's configured as master

  // exit immediately if CAN bus errors (CAN doesn't send messages anymore)
  if( MBNET_BusErrorCheck() < 0 )
    return -3; // transmission error

  // check for incoming acknowledge messages
  mbnet_packet_t p;
  s32 got_msg;
  do {
    got_msg = MBNET_HAL_ReceiveAck(&p);

    if( got_msg > 0 ) {
      // get EID, MSG and DLC
      *dlc = p.dlc;
      *ack_msg = p.msg;

      // response from expected slave? if not - ignore it!
      if( (p.id.control & 0xff) == slave_id ) {
#if DEBUG_VERBOSE_LEVEL >= 3
	DEBUG_MSG("[MBNET] got ACK from slave ID %02x TOS=%d DLC=%d MSG=%02x %02x %02x %02x %02x %02x %02x %02x\n",
		  p.id.control & 0xff,
		  p.id.tos,
		  *dlc,
		  ack_msg->bytes[0], ack_msg->bytes[1], ack_msg->bytes[2], ack_msg->bytes[3],
		  ack_msg->bytes[4], ack_msg->bytes[5], ack_msg->bytes[6], ack_msg->bytes[7]);
#endif

	// retry requested?
	if( p.id.tos == MBNET_ACK_RETRY ) {
#if DEBUG_VERBOSE_LEVEL >= 3
	  DEBUG_MSG("[MBNET] Slave ID %02x requested to retry the transfer!\n", slave_id);
#endif
	  return -4; // slave requested to retry
	}

	return 0; // wait ack successful!
      } else {
#if DEBUG_VERBOSE_LEVEL >= 1
	DEBUG_MSG("[MBNET] ERROR: ACK from unexpected slave ID %02x (TOS=%d DLC=%d MSG=%02x %02x %02x...)\n",
		  p.id.control & 0xff,
		  p.id.tos,
		  *dlc,
		  ack_msg->bytes[0], ack_msg->bytes[1], ack_msg->bytes[2]);
#endif
      }
    }
  } while( got_msg );

  return -5; // no response from slave yet (check again or timeout!)
}


/////////////////////////////////////////////////////////////////////////////
// Checks for acknowledge from a specific slave
// (blocking function)
// IN: <slave_id>: slave node ID (0x00..0x7f)
//     <*msg>: received MBNet message (see mbnet_msg_t structure)
//     <*dlc>: received data field length (0..8)
// OUT: returns 0 if message received successfully
//      returns -1 if this node hasn't been configured yet
//      returns -2 if this node isn't configured as master
//      returns -3 on transmission error
//      returns -6 on timeout (no response within 10 mS)
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_WaitAck(u8 slave_id, mbnet_msg_t *ack_msg, u8 *dlc)
{
  // TODO: timeout after 10 mS, use system timer!
  u32 timeout_ctr = 0;
  s32 error;

  do {
    error = MBNET_WaitAck_NonBlocking(slave_id, ack_msg, dlc);
    if( error == -4 ) {
      MBNET_SendReqAgain(slave_id);
      timeout_ctr = 0;
    }
  } while( (error == -4 || error == -5) && ++timeout_ctr < MBNET_TIMEOUT_CTR_MAX );

  if( timeout_ctr == MBNET_TIMEOUT_CTR_MAX ) {
#if DEBUG_VERBOSE_LEVEL >= 3
    DEBUG_MSG("[MBNET] ACK polling for slave ID %02x timed out!\n", slave_id);
#endif
    return -6; // timeout
  }

  return error;
}


/////////////////////////////////////////////////////////////////////////////
// Handles CAN messages, should be called periodically to check the BUS
// state and to check for incoming messages
//
// Application specific ETOS, READ, WRITE and PING requests are forwarded to
// the callback function with following parameters:
//   void <callback>(u8 master_id, u8 tos, u16 control, mbnet_msg_t req_msg, u8 dlc)
// In any case, the function has to send an acknowledge message back
// to the master node!
//
// IN: pointer to callback function
// OUT: returns < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_Handler(void (*callback)(u8 master_id, mbnet_tos_req_t tos, u16 control, mbnet_msg_t req_msg, u8 dlc))
{
  if( my_node_id >= 128 )
    return -1; // node not configured

  // scan for slave nodes if this node is a master
  if( !scan_finished && (my_node_id & 0x0f) == 0 ) {
    
    // determine next slave node
    u8 again = 0;
    do {
#if MBNET_SLAVE_NODES_BEGIN > 0
      if( search_slave_id < MBNET_SLAVE_NODES_BEGIN || search_slave_id >= MBNET_SLAVE_NODES_END )
#else
      if( search_slave_id >= MBNET_SLAVE_NODES_END )
#endif
	search_slave_id = MBNET_SLAVE_NODES_BEGIN-1; // restart

      // next slave
      ++search_slave_id;

      again = (search_slave_id == my_node_id) ? 1 : 0; // skip my own ID
    } while( again );

    // search slave if not found yet, and retry counter hasn't reached end yet
    u8 ix_ix = search_slave_id-MBNET_SLAVE_NODES_BEGIN;
    if( slave_nodes_ix[ix_ix] >= 128 && slave_nodes_ix[ix_ix] != 0xff ) {
      // increment retry counter
      ++slave_nodes_ix[ix_ix];

      // send a ping, wait for a pong
      mbnet_msg_t req_msg;
      req_msg.data_l = 0;
      req_msg.data_h = 0;
      if( MBNET_SendReq(search_slave_id, MBNET_REQ_PING, 0x0000, req_msg, 0) == 1 ) {
	mbnet_msg_t ack_msg;
	u8 dlc;

	if( MBNET_WaitAck(search_slave_id, &ack_msg, &dlc) == 0 ) {

	  // got it! :-)
	  slave_nodes_ix[ix_ix] = search_slave_id;

	  // try to put it into info array
	  int i;
	  for(i=0; i<MBNET_SLAVE_NODES_MAX; ++i) {
	    if( slave_nodes_info[i].protocol_version == 0 ) {
	      slave_nodes_info[i].data_l = ack_msg.data_l;
	      slave_nodes_info[i].data_h = ack_msg.data_h;
	      slave_nodes_ix[ix_ix] = i;
	      break;
	    }
	  }

	  // still a free slot?
	  if( i >= MBNET_SLAVE_NODES_MAX ) {
	    slave_nodes_ix[ix_ix] = 0xff; // disable retry counter
	  }

#if DEBUG_VERBOSE_LEVEL >= 2
	  DEBUG_MSG("[MBNET] new slave found: ID %02x (Ix=%d) P:%d T:%c%c%c%c V:%d.%d\n", 
		    search_slave_id,
		    slave_nodes_ix[ix_ix],
		    ack_msg.protocol_version,
		    ack_msg.node_type[0], ack_msg.node_type[1], ack_msg.node_type[2], ack_msg.node_type[3],
		    ack_msg.node_version, ack_msg.node_subversion);

	  if( slave_nodes_ix[ix_ix] == 0xff )
	    DEBUG_MSG("[MBNET] unfortunately no free info slot anymore!\n");
#endif
	}
      }
    }

    // check if scan is finished
    {
      int i;
      u8 finished = 1; // taking temp. local variable, since scan_finished could be read by another thread
      for(i=0; i<=MBNET_SLAVE_NODES_END-MBNET_SLAVE_NODES_BEGIN; ++i)
	if( slave_nodes_ix[i] >= 128 && slave_nodes_ix[i] != 0xff && (i+MBNET_SLAVE_NODES_BEGIN) != my_node_id )
	  finished = 0;

      if( finished && !scan_finished ) {
#if DEBUG_VERBOSE_LEVEL >= 2
	DEBUG_MSG("[MBNET] Scan finished.\n");
#endif
	scan_finished = 1;
      }
    }
  }

  u8 got_msg;
  mbnet_packet_t p;
  s8 locked = -1; // contains MS node ID if slave has been locked, otherwise -1 if not locked
#if DEBUG_VERBOSE_LEVEL >= 2
  tos12_ctr = 0; // only for debugging
#endif
  do {
    got_msg = MBNET_HAL_ReceiveReq(&p);

    if( got_msg > 0 ) {
      // master node ID
      u8 master_id = p.id.ms << 4;

#if DEBUG_VERBOSE_LEVEL >= 2
      if( p.id.tos == 1 || p.id.tos == 2 ) {
	++tos12_ctr;
	if( tos12_ctr > 8 ) {
	  DEBUG_MSG("[MBNET] ... skip display of remaining read/write operations\n");
	}
      } else {
	tos12_ctr = 0;
      }
#endif
#if DEBUG_VERBOSE_LEVEL >= 3
      if( tos12_ctr <= 8 ) {
	DEBUG_MSG("[MBNET] request ID=%02x TOS=%d CTRL=%04x DLC=%d MSG=%02x %02x %02x %02x %02x %02x %02x %02x\n",
		  master_id,
		  p.id.tos,
		  p.id.control,
		  p.dlc,
		  p.msg.bytes[0], p.msg.bytes[1], p.msg.bytes[2], p.msg.bytes[3],
		  p.msg.bytes[4], p.msg.bytes[5], p.msg.bytes[6], p.msg.bytes[7]);
      }
#endif
      // default acknowledge message is empty
      mbnet_msg_t ack_msg;
      ack_msg.data_l = ack_msg.data_h = 0;

      // if slave has been locked by another master: send retry acknowledge
      if( locked != -1 && p.id.ms != locked ) {
	MBNET_SendAck(master_id, MBNET_ACK_RETRY, ack_msg, 0); // master_id, tos, msg, dlc
#if DEBUG_VERBOSE_LEVEL >= 3
	DEBUG_MSG("[MBNET] ignore - node is locked by MS=%d\n", locked);
#endif
      } else {
	// branch depending on TOS
	switch( p.id.tos ) {
          case MBNET_REQ_SPECIAL:
	    // branch depending on ETOS (extended TOS)
	    switch( p.id.control & 0xff ) {
	      case 0x00: // lock receiver
		locked = p.id.ms;
		MBNET_SendAck(master_id, MBNET_ACK_OK, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x01: // unlock receiver
		locked = -1;
		MBNET_SendAck(master_id, MBNET_ACK_OK, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x02: // reserved
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x03: // reserved
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x04: // call APP_Init
		APP_Init();
		MBNET_SendAck(master_id, MBNET_ACK_OK, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x05: // call APP_MPROC_DebugTrigger not implemented
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x06: // call APP_MIDI_NotifyPackage
		if( p.dlc != 3 ) {
		  MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		} else {
		  mios32_midi_package_t package;
		  APP_MIDI_NotifyPackage(0, package); // TODO: we could forward the port number as well
		  MBNET_SendAck(master_id, MBNET_ACK_OK, ack_msg, 0); // master_id, tos, msg, dlc
		}
		break;
	      case 0x07: // call APP_NotifyReceivedSysEx
		// not for MIOS32
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x08: // call APP_DIN_NotifyToggle
		if( p.dlc != 2 ) {
		  MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		} else {
		  APP_DIN_NotifyToggle(p.msg.bytes[0], p.msg.bytes[1]);
		  MBNET_SendAck(master_id, MBNET_ACK_OK, ack_msg, 0); // master_id, tos, msg, dlc
		}
		break;
      	      case 0x09: // call APP_ENC_NotifyChange
		if( p.dlc != 2 ) {
		  MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		} else {
		  APP_ENC_NotifyChange(p.msg.bytes[0], p.msg.bytes[1]);
		  MBNET_SendAck(master_id, MBNET_ACK_OK, ack_msg, 0); // master_id, tos, msg, dlc
		}
		break;
      	      case 0x0a: // call APP_AIN_NotifyChange
		if( p.dlc != 3 ) {
		  MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		} else {
		  APP_ENC_NotifyChange(p.msg.bytes[0], p.msg.bytes[1] | (p.msg.bytes[2] << 8));
		  MBNET_SendAck(master_id, MBNET_ACK_OK, ack_msg, 0); // master_id, tos, msg, dlc
		}
		break;
      	      case 0x0b: // reserved
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x0c: // reserved
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x0d: // reserved
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x0e: // reserved
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
      	      case 0x0f: // cloning not supported
		MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
		break;
    	      default:
		// send ETOS to application via callback hook
		callback(p.id.ms << 4, p.id.tos, p.id.control, p.msg, p.dlc);
		break;
	    }
	    break;
  
	  case MBNET_REQ_RAM_READ:
          case MBNET_REQ_RAM_WRITE:
          case MBNET_REQ_PING:
	    // handled by application
	    callback(p.id.ms << 4, p.id.tos, p.id.control, p.msg, p.dlc);
	    break;

	  default: // p.id.tos>=4 will never be received, just for the case...
	    MBNET_SendAck(master_id, MBNET_ACK_ERROR, ack_msg, 0); // master_id, tos, msg, dlc
	    break;
        }  
      }
    }
  } while( got_msg || locked != -1);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Local function:
// Used during transmit or receive polling to determine if a bus error has occured
// (e.g. receiver passive or no nodes connected to bus)
// In this case, all pending transmissions will be aborted
// The mbnet_state.PANIC flag is set to report to the application, that the
// bus is not ready for transactions (flag accessible via MBNET_ErrorStateGet).
// This flag will be cleared by WaitAck once we got back a message from any slave
// OUT: returns -1 if bus permanent off (requires re-initialisation)
//      returns -2 if panic state reached
/////////////////////////////////////////////////////////////////////////////
static s32 MBNET_BusErrorCheck(void)
{
  // check if permanent off state is reached
  if( mbnet_state.PERMANENT_OFF )
    return -1; // requires re-initialisation

  s32 status = MBNET_HAL_BusErrorCheck();
  if( status >= 0 )
    return 0; // no error

  // notify that an abort has happened
  mbnet_state.PANIC = 1;

  // TODO: try to recover

#if DEBUG_VERBOSE_LEVEL >= 1
  DEBUG_MSG("[MBNET] ERRORs detected - permanent off state reached!\n");
#endif

  // recovery not possible: prevent MBNet accesses
  mbnet_state.PERMANENT_OFF = 1;

  return -1; // bus permanent off
}


/////////////////////////////////////////////////////////////////////////////
// Installs an optional Tx Handler which is called via interrupt whenever
// a new message can be sent
// Currently only supported by LPC17xx HAL!
// tx_handler_callback == NULL will disable the handler
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_InstallTxHandler(s32 (*tx_handler_callback)(mbnet_id_t *mbnet_id, mbnet_msg_t *msg, u8 *dlc))
{
  return MBNET_HAL_InstallTxHandler(tx_handler_callback);
}

/////////////////////////////////////////////////////////////////////////////
// Manual request of transmit handler
// Has to be called whenever the transmission has been stopped to restart
// interrupt driven transfers
/////////////////////////////////////////////////////////////////////////////
s32 MBNET_TriggerTxHandler(void)
{
  return MBNET_HAL_TriggerTxHandler();
}