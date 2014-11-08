// $Id: midimon.h 662 2009-07-25 22:40:36Z tk $
/*
 * Header file for MIDI monitor module
 *
 * ==========================================================================
 *
 *  Copyright (C) 2009 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _MIDIMON_H
#define _MIDIMON_H


/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 MIDIMON_Init(u32 mode);
extern s32 MIDIMON_Receive(mios32_midi_port_t port, mios32_midi_package_t package, u32 timestamp, u8 filter_sysex_message);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////


#endif /* _MIDIMON_H */
