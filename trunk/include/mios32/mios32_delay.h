// $Id: mios32_delay.h 144 2008-12-02 00:07:05Z tk $
/*
 * Header file for Delay functions
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _MIOS32_DELAY_H
#define _MIOS32_DELAY_H

/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////


// timer used for MIOS32_DELAY functions (TIM1..TIM8)
#ifndef MIOS32_DELAY_TIMER
#define MIOS32_DELAY_TIMER  TIM1
#endif

#ifndef MIOS32_DELAY_TIMER_RCC
#define MIOS32_DELAY_TIMER_RCC RCC_APB2Periph_TIM1
#endif


/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 MIOS32_DELAY_Init(u32 mode);
extern s32 MIOS32_DELAY_Wait_uS(u16 uS);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////


#endif /* _MIOS32_DELAY_H */
