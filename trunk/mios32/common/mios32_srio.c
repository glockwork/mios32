// $Id: mios32_srio.c 1127 2010-11-21 21:52:36Z tk $
//! \defgroup MIOS32_SRIO
//!
//! SRIO Driver for MIOS32
//! \{
/* ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>

// this module can be optionally disabled in a local mios32_config.h file (included from mios32.h)
#if !defined(MIOS32_DONT_USE_SRIO)


/////////////////////////////////////////////////////////////////////////////
// Global variables
/////////////////////////////////////////////////////////////////////////////

// DOUT SR registers in reversed (!) order (since DMA doesn't provide a decrement address function)
// Note that also the bits are in reversed order compared to PIC based MIOS
// As long as the array is accessed via MIOS32_DOUT_* functions, they programmer won't notice a difference!
volatile u8 mios32_srio_dout[MIOS32_SRIO_NUM_SR];

// DIN values of last scan
volatile u8 mios32_srio_din[MIOS32_SRIO_NUM_SR];

// DIN values of ongoing scan
// Note: during SRIO scan it is required to copy new DIN values into a temporary buffer
// to avoid that a task already takes a new DIN value before the whole chain has been scanned
// (e.g. relevant for encoder handler: it has to clear the changed flags, so that the DIN handler doesn't take the value)
volatile u8 mios32_srio_din_buffer[MIOS32_SRIO_NUM_SR];

// change notification flags
volatile u8 mios32_srio_din_changed[MIOS32_SRIO_NUM_SR];

// for debouncing
static u8 debounce_time;
static u8 debounce_ctr;



/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static void (*srio_scan_finished_hook)(void);

static volatile u8 srio_values_transfered;


/////////////////////////////////////////////////////////////////////////////
// Local prototypes
/////////////////////////////////////////////////////////////////////////////

static void MIOS32_SRIO_DMA_Callback(void);


/////////////////////////////////////////////////////////////////////////////
//! Initializes SPI pins and peripheral
//! \param[in] mode currently only mode 0 supported
//! \return < 0 if initialisation failed
/////////////////////////////////////////////////////////////////////////////
s32 MIOS32_SRIO_Init(u32 mode)
{
  // currently only mode 0 supported
  if( mode != 0 )
    return -1; // unsupported mode

  u8 i;

  // disable notification hook
  srio_scan_finished_hook = NULL;

  // clear chains
  // will be done again in MIOS32_DIN_Init and MIOS32_DOUT_Init
  // we don't reference to these functions here to allow the programmer to remove/replace these driver modules)
  for(i=0; i<MIOS32_SRIO_NUM_SR; ++i) {
    mios32_srio_dout[i] = 0x00;       // passive state (LEDs off)
    mios32_srio_din[i] = 0xff;        // passive state (Buttons depressed)
    mios32_srio_din_buffer[i] = 0xff; // passive state (Buttons depressed)
    mios32_srio_din_changed[i] = 0;   // no change
  }

  // initial state of RCLK
  MIOS32_SPI_RC_PinSet(MIOS32_SRIO_SPI, MIOS32_SRIO_SPI_RC_PIN, 1); // spi, rc_pin, pin_value

  // init GPIO structure
  // using 2 MHz instead of 50 MHz to avoid fast transients which can cause flickering!
  // optionally using open drain mode for cheap and sufficient levelshifting from 3.3V to 5V
#if MIOS32_SRIO_OUTPUTS_OD
  MIOS32_SPI_IO_Init(MIOS32_SRIO_SPI, MIOS32_SPI_PIN_DRIVER_WEAK_OD);
#else
  MIOS32_SPI_IO_Init(MIOS32_SRIO_SPI, MIOS32_SPI_PIN_DRIVER_WEAK);
#endif

  // init SPI port for baudrate of ca. 2 uS period @ 72 MHz
  MIOS32_SPI_TransferModeInit(MIOS32_SRIO_SPI, MIOS32_SPI_MODE_CLK1_PHASE1, MIOS32_SPI_PRESCALER_64);

  // notify that SRIO values have been transfered
  // (cleared on each ScanStart, set on each DMA IRQ invokation for proper synchronisation)
  srio_values_transfered = 1;

  // initial debounce time (debouncing disabled)
  debounce_time = 0;
  debounce_ctr = 0;
  
  return 0;
}


/////////////////////////////////////////////////////////////////////////////
//! Returns the debounce counter reload value of the DIN SR registers
//! \return debounce counter reload value (0 if disabled, otherwise 1..255)
/////////////////////////////////////////////////////////////////////////////
u32 MIOS32_SRIO_DebounceGet(void)
{
  return debounce_time;
}

/////////////////////////////////////////////////////////////////////////////
//! Sets the debounce counter reload value for the DIN SR registers which are 
//! not assigned to rotary encoders (or other drivers which get use of
//! MIOS32_DIN_SRChangedGetAndClear()) to debounce low-quality buttons.
//!
//! Debouncing is realized in the following way: on every button movement 
//! the debounce preload value will be loaded into the debounce counter 
//! register. The counter will be decremented on every SRIO update cycle (usually 1 mS)
//! As long as this counter isn't zero, button changes will still be recorded, 
//! but they won't trigger the APP_DIN_NotifyToggle hook.
//!
//! No (intended) button movement will get lost, but the latency will be 
//! increased. Example: if the update frequency is set to 1 mS, and the 
//! debounce value to 32, the first button movement will be regognized 
//! with a worst-case latency of 1 mS. Every additional button movement 
//! which happens within 32 mS will be regognized within a worst-case 
//! latency of 32 mS. After the debounce time has passed, the worst-case 
//! latency is 1 mS again.
//!
//! This function affects all DIN registers. If the application should 
//! record pin changes from digital sensors which are switching very fast, 
//! then debouncing should be ommited.
//!
//! \param[in] debounce_time delay in mS (1..255)<BR>
//!            0 disables debouncing (default)
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 MIOS32_SRIO_DebounceSet(u8 _debounce_time)
{
  debounce_time = _debounce_time;

  // lower counter value if new value is less than old one
  if( debounce_ctr > debounce_time )
    debounce_ctr = debounce_time;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
//! Internally used function to start the debounce delay after a button
//! has been moved.<BR>
//! This function is used by MIOS32_DIN_Handler(), there is no need to use
//! it in a common application.
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 MIOS32_SRIO_DebounceStart(void)
{
  debounce_ctr = debounce_time;
  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
//! (Re-)Starts the SPI IRQ Handler which scans the SRIO chain
//! \param[in] _notify_hook notification function which will be called after the scan has been finished
//!     (all DOUT registers written, all DIN registers read)
//!     use NULL if no function should be called
//! \return < 0 if operation failed
/////////////////////////////////////////////////////////////////////////////
s32 MIOS32_SRIO_ScanStart(void *_notify_hook)
{
#if MIOS32_SRIO_NUM_SR == 0
  return -1; // no SRIO scan required
#endif

  // exit if previous stream hasn't been sent yet (no additional transfer required)
  // THIS IS A FAILSAVE MEASURE ONLY!
  // should never happen if MIOS32_SRIO_ScanStart is called each mS
  // the transfer itself takes ca. 225 uS (if 16 SRIOs are scanned)
  if( !srio_values_transfered )
    return -2; // notify this special scenario - we could retry here

  // notify that new values have to be transfered
  srio_values_transfered = 0;

  // change notification function
  srio_scan_finished_hook = _notify_hook;

  // before first byte will be sent:
  // latch DIN registers by pulsing RCLK: 1->0->1
  // TODO: maybe we should disable all IRQs here for higher accuracy
  MIOS32_SPI_RC_PinSet(MIOS32_SRIO_SPI, MIOS32_SRIO_SPI_RC_PIN, 0); // spi, rc_pin, pin_value
  MIOS32_DELAY_Wait_uS(1); // TODO: variable delay for touch sensors
  MIOS32_SPI_RC_PinSet(MIOS32_SRIO_SPI, MIOS32_SRIO_SPI_RC_PIN, 1); // spi, rc_pin, pin_value

  // start DMA transfer
  MIOS32_SPI_TransferBlock(MIOS32_SRIO_SPI,
			   (u8 *)&mios32_srio_dout[0], (u8 *)&mios32_srio_din_buffer[0],
			   MIOS32_SRIO_NUM_SR,
			   MIOS32_SRIO_DMA_Callback);

  return 0;
}


/////////////////////////////////////////////////////////////////////////////
// DMA callback function is called by MIOS32_SPI driver once the complete SRIO chain
// has been scanned
/////////////////////////////////////////////////////////////////////////////
static void MIOS32_SRIO_DMA_Callback(void)
{
  // notify that new values have been transfered
  srio_values_transfered = 1;

  // latch DOUT registers by pulsing RCLK: 1->0->1
  MIOS32_SPI_RC_PinSet(MIOS32_SRIO_SPI, MIOS32_SRIO_SPI_RC_PIN, 0); // spi, rc_pin, pin_value
  MIOS32_DELAY_Wait_uS(1);
  MIOS32_SPI_RC_PinSet(MIOS32_SRIO_SPI, MIOS32_SRIO_SPI_RC_PIN, 1); // spi, rc_pin, pin_value

  // copy/or buffered DIN values/changed flags
  int i;
  for(i=0; i<MIOS32_SRIO_NUM_SR; ++i) {
    mios32_srio_din_changed[i] |= mios32_srio_din[i] ^ mios32_srio_din_buffer[i];
    mios32_srio_din[i] = mios32_srio_din_buffer[i];
  }

  // call user specific hook if requested
  // it has to be called before button debouncing is handled
  // to ensure that the encoder driver, but also other drivers (e.g. BLM) are working properly
  // regardless if debouncing is enabled or not
  if( srio_scan_finished_hook != NULL )
    srio_scan_finished_hook();

  // As long as debounce counter is != 0, clear all "changed" flags to ignore button movements 
  // at this time. In order to ensure, that a new final state of a button won't get lost, 
  // the DIN values are XORed with the "changed" flags (yes, this idea is ill, but it works! :)
  // Even the encoder handler (or others which are notified by the scan_finished_hook) still
  // work properly, because they are clearing the appr. "changed" flags, so that the DIN
  // values won't be touched by the XOR operation.
  if( debounce_time && debounce_ctr ) {
    --debounce_ctr;

    for(i=0; i<MIOS32_SRIO_NUM_SR; ++i) {
      mios32_srio_din[i] ^= mios32_srio_din_changed[i];
      mios32_srio_din_changed[i] = 0;
    }
  }

  // next transfer has to be started with MIOS32_SRIO_ScanStart
}

//! \}

#endif /* MIOS32_DONT_USE_SRIO */

