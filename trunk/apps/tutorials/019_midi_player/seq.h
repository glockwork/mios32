// $Id: seq.h 693 2009-08-07 21:11:27Z tk $
/*
 * Header file for sequencer routines
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _SEQ_H
#define _SEQ_H

/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_Init(u32 mode);

extern s32 SEQ_Reset(void);
extern s32 SEQ_Handler(void);

extern s32 SEQ_PlayFileReq(u32 next);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////

static u8 seq_pause;


#endif /* _SEQ_H */
