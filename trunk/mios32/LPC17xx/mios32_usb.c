// $Id: mios32_usb.c 1191 2011-04-25 19:06:07Z tk $
//! \defgroup MIOS32_USB
//!
//! USB driver for MIOS32
//! 
//! Uses LPCUSB driver from Bertrik Sikken (bertrik@sikken.nl)
//! which is installed under $MIOS32_PATH/drivers/LPC17xx/usbstack
//! -> http://sourceforge.net/projects/lpcusb
//! 
//! Applications shouldn't call these functions directly, instead please use \ref MIOS32_COM or \ref MIOS32_MIDI layer functions
//! 
//! \{
/* ==========================================================================
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

// this module can be optionally disabled in a local mios32_config.h file (included from mios32.h)
#if !defined(MIOS32_DONT_USE_USB)

#include <string.h>
#include <usbapi.h>


/////////////////////////////////////////////////////////////////////////////
// Local definitions
/////////////////////////////////////////////////////////////////////////////

#define DSCR_DEVICE	1	// Descriptor type: Device
#define DSCR_CONFIG	2	// Descriptor type: Configuration
#define DSCR_STRING	3	// Descriptor type: String
#define DSCR_INTRFC	4	// Descriptor type: Interface
#define DSCR_ENDPNT	5	// Descriptor type: Endpoint

#define CS_INTERFACE	0x24	// Class-specific type: Interface
#define CS_ENDPOINT	0x25	// Class-specific type: Endpoint

#define LE_WORD(x)              ((x)&0xFF),((x)>>8)

#if 0
// ISTR events
// mask defining which events has to be handled by the device application software
//#define IMR_MSK (CNTR_CTRM  | CNTR_SOFM  | CNTR_RESETM)
#define IMR_MSK (CNTR_CTRM | CNTR_RESETM)
// TK: disabled SOF interrupt, since it isn't really used and disturbs debugging
#endif


/////////////////////////////////////////////////////////////////////////////
// Local types
/////////////////////////////////////////////////////////////////////////////

#if 0
typedef enum _DEVICE_STATE
{
  UNCONNECTED,
  ATTACHED,
  POWERED,
  SUSPENDED,
  ADDRESSED,
  CONFIGURED
} DEVICE_STATE;
#endif

/////////////////////////////////////////////////////////////////////////////
// Global Variables used by STM32 USB Driver
// (unfortunately no unique names are used...)
/////////////////////////////////////////////////////////////////////////////

#define SET_LINE_CODING                 0x20
#define GET_LINE_CODING                 0x21
#define SET_CONTROL_LINE_STATE  0x22

// data structure for GET_LINE_CODING / SET_LINE_CODING class requests
typedef struct {
        U32             dwDTERate;
        U8              bCharFormat;
        U8              bParityType;
        U8              bDataBits;
} TLineCoding;

static TLineCoding LineCoding = {115200, 0, 0, 8};
static U8 abClassReqData[8];

#if 0
/*  Points to the DEVICE_INFO/DEVICE_PROP_USER_STANDARD_REQUESTS structure of current device */
/*  The purpose of this register is to speed up the execution */
// TK: in addition, this allows to change USB driver during runtime
DEVICE_INFO *pInformation;
DEVICE Device_Table;
DEVICE_PROP *pProperty;
USER_STANDARD_REQUESTS *pUser_Standard_Requests;

// stored in RAM, vectors can be changed on-the-fly
void (*pEpInt_IN[7])(void) = {
  NOP_Process, NOP_Process, NOP_Process, NOP_Process, NOP_Process, NOP_Process, NOP_Process
};

void (*pEpInt_OUT[7])(void) = {
  NOP_Process, NOP_Process, NOP_Process, NOP_Process, NOP_Process, NOP_Process, NOP_Process
};
#endif

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


// a lot confusing settings, which could also be generated via software!
#ifndef MIOS32_DONT_USE_USB_MIDI
# if MIOS32_USB_MIDI_USE_AC_INTERFACE
#  define MIOS32_USB_MIDI_NUM_INTERFACES        2
#  define MIOS32_USB_MIDI_AC_INTERFACE_IX       0x00
#  define MIOS32_USB_MIDI_AS_INTERFACE_IX       0x01
#  define MIOS32_USB_MIDI_INTERFACE_OFFSET      2
# else
#  define MIOS32_USB_MIDI_NUM_INTERFACES        1
#  define MIOS32_USB_MIDI_INTERFACE_NUM         0x00
#  define MIOS32_USB_MIDI_INTERFACE_OFFSET      1
# endif
# define MIOS32_USB_MIDI_SIZ_CLASS_DESC         (7+MIOS32_USB_MIDI_NUM_PORTS*(6+6+9+9)+9+(4+MIOS32_USB_MIDI_NUM_PORTS)+9+(4+MIOS32_USB_MIDI_NUM_PORTS))
# define MIOS32_USB_MIDI_SIZ_CONFIG_DESC        (9+MIOS32_USB_MIDI_USE_AC_INTERFACE*(9+9)+MIOS32_USB_MIDI_SIZ_CLASS_DESC)
#else
# define MIOS32_USB_MIDI_NUM_INTERFACES         0
# define MIOS32_USB_MIDI_INTERFACE_OFFSET       0
# define MIOS32_USB_MIDI_SIZ_CONFIG_DESC        0
#endif

#ifdef MIOS32_USE_USB_COM
# define MIOS32_USB_COM_NUM_INTERFACES          2
# define MIOS32_USB_COM_SIZ_CONFIG_DESC         58
# define MIOS32_USB_COM_CC_INTERFACE_IX         (MIOS32_USB_MIDI_INTERFACE_OFFSET + 0x00)
# define MIOS32_USB_COM_CD_INTERFACE_IX         (MIOS32_USB_MIDI_INTERFACE_OFFSET + 0x01)
# define MIOS32_USB_COM_INTERFACE_OFFSET        (MIOS32_USB_MIDI_INTERFACE_OFFSET + 2)
#else
# define MIOS32_USB_COM_NUM_INTERFACES          0
# define MIOS32_USB_COM_SIZ_CONFIG_DESC         0
# define MIOS32_USB_COM_INTERFACE_OFFSET        (MIOS32_USB_MIDI_INTERFACE_OFFSET + 0)
#endif

#define MIOS32_USB_NUM_INTERFACES              (MIOS32_USB_MIDI_NUM_INTERFACES + MIOS32_USB_COM_NUM_INTERFACES)
#define MIOS32_USB_SIZ_CONFIG_DESC             (9 + MIOS32_USB_MIDI_SIZ_CONFIG_DESC + MIOS32_USB_COM_SIZ_CONFIG_DESC)

#define MIOS32_USB_SIZ_DEVICE_DESC 18


/////////////////////////////////////////////////////////////////////////////
// USB Config Descriptor
/////////////////////////////////////////////////////////////////////////////

// TODO: generate the config descriptor via software
// this has to be done in *MIOS32_USB_CB_GetConfigDescriptor()
// Problem: it would increase stack or static RAM consumption

//static const u8 MIOS32_USB_ConfigDescriptor[MIOS32_USB_SIZ_DEVICE_DESC + MIOS32_USB_SIZ_CONFIG_DESC] = {
static const u8 MIOS32_USB_ConfigDescriptor[] = {
  (u8)(MIOS32_USB_SIZ_DEVICE_DESC&0xff), // Device Descriptor length
  DSCR_DEVICE,			// Decriptor type
  (u8)(0x0200 & 0xff),		// Specification Version (BCD, LSB)
  (u8)(0x0200 >> 8),		// Specification Version (BCD, MSB)
#if 1
  0x02,				// Device class "Communication"   -- required for MacOS to find the COM device. Audio Device works fine in parallel to this
#else
  0x00,				// Device class "Composite"
#endif
  0x00,				// Device sub-class
  0x00,				// Device sub-sub-class
  0x40,				// Maximum packet size
  (u8)((MIOS32_USB_VENDOR_ID) & 0xff),  // Vendor ID (LSB)
  (u8)((MIOS32_USB_VENDOR_ID) >> 8),    // Vendor ID (MSB)
  (u8)((MIOS32_USB_PRODUCT_ID) & 0xff),	// Product ID (LSB)
  (u8)((MIOS32_USB_PRODUCT_ID) >> 8),	// Product ID (MSB)
  (u8)((MIOS32_USB_VERSION_ID) & 0xff),	// Product version ID (LSB)
  (u8)((MIOS32_USB_VERSION_ID) >> 8),  	// Product version ID (MSB)
  0x01,				// Manufacturer string index
  0x02,				// Product string index
  0x03,				// Serial number string index
  0x01,				// Number of configurations


  // Configuration Descriptor
  9,				// Descriptor length
  DSCR_CONFIG,			// Descriptor type
  (MIOS32_USB_SIZ_CONFIG_DESC) & 0xff,  // Config + End Points length (LSB)
  (MIOS32_USB_SIZ_CONFIG_DESC) >> 8,    // Config + End Points length (LSB)
  MIOS32_USB_NUM_INTERFACES,    // Number of interfaces
  0x01,				// Configuration Value
  0x00,				// Configuration string
  0x80,				// Attributes (b7 - buspwr, b6 - selfpwr, b5 - rwu)
  0x32,				// Power requirement (div 2 ma)


  ///////////////////////////////////////////////////////////////////////////
  // USB MIDI
  ///////////////////////////////////////////////////////////////////////////
#ifndef MIOS32_DONT_USE_USB_MIDI

#if MIOS32_USB_MIDI_USE_AC_INTERFACE
  // Standard AC Interface Descriptor
  9,				// Descriptor length
  DSCR_INTRFC,			// Descriptor type
  MIOS32_USB_MIDI_AC_INTERFACE_IX, // Zero-based index of this interface
  0x00,				// Alternate setting
  0x00,				// Number of end points 
  0x01,				// Interface class  (AUDIO)
  0x01,				// Interface sub class  (AUDIO_CONTROL)
  0x00,				// Interface sub sub class
  0x02,				// Interface descriptor string index

  // MIDI Adapter Class-specific AC Interface Descriptor
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type
  0x01,				// Header subtype
  0x00,				// Revision of class specification - 1.0 (LSB)
  0x01,				// Revision of class specification (MSB)
  9,				// Total size of class-specific descriptors (LSB)
  0,				// Total size of class-specific descriptors (MSB)
  0x01,				// Number of streaming interfaces
  0x01,				// MIDI Streaming Interface 1 belongs to this AudioControl Interface
#endif

  // Standard MS Interface Descriptor
  9,				// Descriptor length
  DSCR_INTRFC,			// Descriptor type
  MIOS32_USB_MIDI_AS_INTERFACE_IX, // Zero-based index of this interface
  0x00,				// Alternate setting
  0x02,				// Number of end points 
  0x01,				// Interface class  (AUDIO)
  0x03,				// Interface sub class  (MIDISTREAMING)
  0x00,				// Interface sub sub class
  0x02,				// Interface descriptor string index

  // Class-specific MS Interface Descriptor
  7,				// Descriptor length
  CS_INTERFACE,			// Descriptor type
#if MIOS32_USB_MIDI_USE_AC_INTERFACE
  0x01,				// Zero-based index of this interface
#else
  0x00,				// Zero-based index of this interface
#endif
  0x00,				// revision of this class specification (LSB)
  0x01,				// revision of this class specification (MSB)
  (u8)(MIOS32_USB_MIDI_SIZ_CLASS_DESC & 0xff), // Total size of class-specific descriptors (LSB)
  (u8)(MIOS32_USB_MIDI_SIZ_CLASS_DESC >> 8),   // Total size of class-specific descriptors (MSB)


#if MIOS32_USB_MIDI_NUM_PORTS >= 1
  // MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x01,				// ID of this jack
  0x00,				// unused

  // MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x02,				// ID of this jack
  0x00,				// unused

  // MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x03,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x02,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x04,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x01,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


#if MIOS32_USB_MIDI_NUM_PORTS >= 2
  // Second MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x05,				// ID of this jack
  0x00,				// unused

  // Second MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x06,				// ID of this jack
  0x00,				// unused

  // Second MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x07,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x06,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // Second MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x08,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x05,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


#if MIOS32_USB_MIDI_NUM_PORTS >= 3
  // Third MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x09,				// ID of this jack
  0x00,				// unused

  // Third MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x0a,				// ID of this jack
  0x00,				// unused

  // Third MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x0b,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x0a,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // Third MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x0c,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x09,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


#if MIOS32_USB_MIDI_NUM_PORTS >= 4
  // Fourth MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x0d,				// ID of this jack
  0x00,				// unused

  // Fourth MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x0e,				// ID of this jack
  0x00,				// unused

  // Fourth MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x0f,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x0e,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // Fourth MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x10,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x0d,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


#if MIOS32_USB_MIDI_NUM_PORTS >= 5
  // Fifth MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x11,				// ID of this jack
  0x00,				// unused

  // Fifth MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x12,				// ID of this jack
  0x00,				// unused

  // Fifth MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x13,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x12,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // Fifth MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x14,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x11,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


#if MIOS32_USB_MIDI_NUM_PORTS >= 6
  // Sixth MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x15,				// ID of this jack
  0x00,				// unused

  // Sixth MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x16,				// ID of this jack
  0x00,				// unused

  // Sixth MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x17,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x16,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // Sixth MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x18,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x15,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


#if MIOS32_USB_MIDI_NUM_PORTS >= 7
  // Seventh MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x19,				// ID of this jack
  0x00,				// unused

  // Seventh MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x1a,				// ID of this jack
  0x00,				// unused

  // Seventh MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x1b,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x1a,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // Seventh MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x1c,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x19,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


#if MIOS32_USB_MIDI_NUM_PORTS >= 8
  // Eighth MIDI IN Jack Descriptor (Embedded)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x01,				// EMBEDDED
  0x1d,				// ID of this jack
  0x00,				// unused

  // Eighth MIDI Adapter MIDI IN Jack Descriptor (External)
  6,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x02,				// MIDI_IN_JACK subtype
  0x02,				// EXTERNAL
  0x1e,				// ID of this jack
  0x00,				// unused

  // Eighth MIDI Adapter MIDI OUT Jack Descriptor (Embedded)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x01,				// EMBEDDED
  0x1f,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x1e,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused

  // Eighth MIDI Adapter MIDI OUT Jack Descriptor (External)
  9,				// Descriptor length
  CS_INTERFACE,			// Descriptor type (CS_INTERFACE)
  0x03,				// MIDI_OUT_JACK subtype
  0x02,				// EXTERNAL
  0x20,				// ID of this jack
  0x01,				// number of input pins of this jack
  0x1d,				// ID of the entity to which this pin is connected
  0x01,				// Output Pin number of the entity to which this input pin is connected
  0x00,				// unused
#endif


  // Standard Bulk OUT Endpoint Descriptor
  9,				// Descriptor length
  DSCR_ENDPNT,			// Descriptor type
  0x01,				// Out Endpoint 1
  0x02,				// Bulk, not shared
  (u8)(MIOS32_USB_MIDI_DATA_IN_SIZE&0xff),	// num of bytes per packet (LSB)
  (u8)(MIOS32_USB_MIDI_DATA_IN_SIZE>>8),	// num of bytes per packet (MSB)
  0x00,				// ignore for bulk
  0x00,				// unused
  0x00,				// unused

  // Class-specific MS Bulk Out Endpoint Descriptor
  4+MIOS32_USB_MIDI_NUM_PORTS,	// Descriptor length
  CS_ENDPOINT,			// Descriptor type (CS_ENDPOINT)
  0x01,				// MS_GENERAL
  MIOS32_USB_MIDI_NUM_PORTS,	// number of embedded MIDI IN Jacks
  0x01,				// ID of embedded MIDI In Jack
#if MIOS32_USB_MIDI_NUM_PORTS >= 2
  0x05,				// ID of embedded MIDI In Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 3
  0x09,				// ID of embedded MIDI In Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 4
  0x0d,				// ID of embedded MIDI In Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 5
  0x11,				// ID of embedded MIDI In Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 6
  0x15,				// ID of embedded MIDI In Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 7
  0x19,				// ID of embedded MIDI In Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 8
  0x1d,				// ID of embedded MIDI In Jack
#endif

  // Standard Bulk IN Endpoint Descriptor
  9,				// Descriptor length
  DSCR_ENDPNT,			// Descriptor type
  0x81,				// In Endpoint 1
  0x02,				// Bulk, not shared
  (u8)(MIOS32_USB_MIDI_DATA_OUT_SIZE&0xff),	// num of bytes per packet (LSB)
  (u8)(MIOS32_USB_MIDI_DATA_OUT_SIZE>>8),	// num of bytes per packet (MSB)
  0x00,				// ignore for bulk
  0x00,				// unused
  0x00,				// unused

  // Class-specific MS Bulk In Endpoint Descriptor
  4+MIOS32_USB_MIDI_NUM_PORTS,	// Descriptor length
  CS_ENDPOINT,			// Descriptor type (CS_ENDPOINT)
  0x01,				// MS_GENERAL
  MIOS32_USB_MIDI_NUM_PORTS,	// number of embedded MIDI Out Jacks
  0x03,				// ID of embedded MIDI Out Jack
#if MIOS32_USB_MIDI_NUM_PORTS >= 2
  0x07,				// ID of embedded MIDI Out Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 3
  0x0b,				// ID of embedded MIDI Out Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 4
  0x0f,				// ID of embedded MIDI Out Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 5
  0x13,				// ID of embedded MIDI Out Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 6
  0x17,				// ID of embedded MIDI Out Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 7
  0x1b,				// ID of embedded MIDI Out Jack
#endif
#if MIOS32_USB_MIDI_NUM_PORTS >= 8
  0x1f,				// ID of embedded MIDI Out Jack
#endif

#endif /* MIOS32_DONT_USE_USB_MIDI */



  ///////////////////////////////////////////////////////////////////////////
  // CDC
  ///////////////////////////////////////////////////////////////////////////
#ifdef MIOS32_USE_USB_COM

  /*Interface Descriptor*/
  0x09,   /* bLength: Interface Descriptor size */
  DSCR_INTRFC,  /* bDescriptorType: Interface */
  /* Interface descriptor type */
  MIOS32_USB_COM_CC_INTERFACE_IX,   /* bInterfaceNumber: Number of Interface */
  0x00,   /* bAlternateSetting: Alternate setting */
  0x01,   /* bNumEndpoints: One endpoints used */
  0x02,   /* bInterfaceClass: Communication Interface Class */
  0x02,   /* bInterfaceSubClass: Abstract Control Model */
  0x01,   /* bInterfaceProtocol: Common AT commands */
  0x00,   /* iInterface: */

  /*Header Functional Descriptor*/
  0x05,   /* bLength: Endpoint Descriptor size */
  CS_INTERFACE, /* bDescriptorType: CS_INTERFACE */
  0x00,   /* bDescriptorSubtype: Header Func Desc */
  0x10,   /* bcdCDC: spec release number */
  0x01,

  /*Call Managment Functional Descriptor*/
  0x05,   /* bFunctionLength */
  CS_INTERFACE, /* bDescriptorType: CS_INTERFACE */
  0x01,   /* bDescriptorSubtype: Call Management Func Desc */
  0x00,   /* bmCapabilities: D0+D1 */
  MIOS32_USB_COM_CD_INTERFACE_IX,   /* bDataInterface */

  /*ACM Functional Descriptor*/
  0x04,   /* bFunctionLength */
  CS_INTERFACE, /* bDescriptorType: CS_INTERFACE */
  0x02,   /* bDescriptorSubtype: Abstract Control Management desc */
  0x02,   /* bmCapabilities */

  /*Union Functional Descriptor*/
  0x05,   /* bFunctionLength */
  CS_INTERFACE, /* bDescriptorType: CS_INTERFACE */
  0x06,   /* bDescriptorSubtype: Union func desc */
  MIOS32_USB_COM_CC_INTERFACE_IX,   /* bMasterInterface: Communication class interface */
  MIOS32_USB_COM_CD_INTERFACE_IX,   /* bSlaveInterface0: Data Class Interface */

  /*Endpoint 2 Descriptor*/
  0x07,   /* bLength: Endpoint Descriptor size */
  DSCR_ENDPNT,  /* bDescriptorType: Endpoint */
  0x82,   /* bEndpointAddress: (IN2) */
  0x03,   /* bmAttributes: Interrupt */
  MIOS32_USB_COM_INT_IN_SIZE,      /* wMaxPacketSize: */
  0x00,
  0xFF,   /* bInterval: */


  /*Data class interface descriptor*/
  0x09,   /* bLength: Endpoint Descriptor size */
  DSCR_INTRFC,  /* bDescriptorType: */
  MIOS32_USB_COM_CD_INTERFACE_IX,   /* bInterfaceNumber: Number of Interface */
  0x00,   /* bAlternateSetting: Alternate setting */
  0x02,   /* bNumEndpoints: Two endpoints used */
  0x0A,   /* bInterfaceClass: CDC */
  0x00,   /* bInterfaceSubClass: */
  0x00,   /* bInterfaceProtocol: */
  0x00,   /* iInterface: */

  /*Endpoint 3 Descriptor*/
  0x07,   /* bLength: Endpoint Descriptor size */
  DSCR_ENDPNT,  /* bDescriptorType: Endpoint */
  0x03,   /* bEndpointAddress: (OUT3) */
  0x02,   /* bmAttributes: Bulk */
  MIOS32_USB_COM_DATA_OUT_SIZE,             /* wMaxPacketSize: */
  0x00,
  0x00,   /* bInterval: ignore for Bulk transfer */

  /*Endpoint 4 Descriptor*/
  0x07,   /* bLength: Endpoint Descriptor size */
  DSCR_ENDPNT,  /* bDescriptorType: Endpoint */
  0x84,   /* bEndpointAddress: (IN4) */
  0x02,   /* bmAttributes: Bulk */
  MIOS32_USB_COM_DATA_IN_SIZE,             /* wMaxPacketSize: */
  0x00,
  0x00,    /* bInterval */

#endif /* MIOS32_USE_USB_COM */

  // string descriptors
  0x04,
  DESC_STRING,
  LE_WORD(0x0409),

  // TODO: allow to change strings as for STM32
  14,
  DESC_STRING,
  'M', 0, 'I', 0, 'O', 0, 'S', 0, '3', 0, '2', 0,

  22,
  DESC_STRING,
  'M', 0, 'I', 0, 'O', 0, 'S', 0, '3', 0, '2', 0, ' ', 0, 'A', 0, 'p', 0, 'p', 0,

  18,
  DESC_STRING,
  '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0, '8', 0,

// terminating zero
        0

};


/////////////////////////////////////////////////////////////////////////////
// Local prototypes
/////////////////////////////////////////////////////////////////////////////

static BOOL HandleClassRequest(TSetupPacket *pSetup, int *piLen, U8 **ppbData);
static void USBFrameHandler(U16 wFrame);

#if 0
static void MIOS32_USB_CB_Reset(void);
static void MIOS32_USB_CB_SetConfiguration(void);
static void MIOS32_USB_CB_SetDeviceAddress (void);
static void MIOS32_USB_CB_Status_In(void);
static void MIOS32_USB_CB_Status_Out(void);
static RESULT MIOS32_USB_CB_Data_Setup(u8 RequestNo);
static RESULT MIOS32_USB_CB_NoData_Setup(u8 RequestNo);
static u8 *MIOS32_USB_CB_GetDeviceDescriptor(u16 Length);
static u8 *MIOS32_USB_CB_GetConfigDescriptor(u16 Length);
static u8 *MIOS32_USB_CB_GetStringDescriptor(u16 Length);
static RESULT MIOS32_USB_CB_Get_Interface_Setting(u8 Interface, u8 AlternateSetting);
#endif

/////////////////////////////////////////////////////////////////////////////
// USB callback vectors
/////////////////////////////////////////////////////////////////////////////

#if 0
static const DEVICE My_Device_Table = {
  MIOS32_USB_EP_NUM,
  1
};

static const DEVICE_PROP My_Device_Property = {
  0, // MIOS32_USB_CB_Init,
  MIOS32_USB_CB_Reset,
  MIOS32_USB_CB_Status_In,
  MIOS32_USB_CB_Status_Out,
  MIOS32_USB_CB_Data_Setup,
  MIOS32_USB_CB_NoData_Setup,
  MIOS32_USB_CB_Get_Interface_Setting,
  MIOS32_USB_CB_GetDeviceDescriptor,
  MIOS32_USB_CB_GetConfigDescriptor,
  MIOS32_USB_CB_GetStringDescriptor,
  0,
  0x40 /*MAX PACKET SIZE*/
};

static const USER_STANDARD_REQUESTS My_User_Standard_Requests = {
  NOP_Process, // MIOS32_USB_CB_GetConfiguration,
  MIOS32_USB_CB_SetConfiguration,
  NOP_Process, // MIOS32_USB_CB_GetInterface,
  NOP_Process, // MIOS32_USB_CB_SetInterface,
  NOP_Process, // MIOS32_USB_CB_GetStatus,
  NOP_Process, // MIOS32_USB_CB_ClearFeature,
  NOP_Process, // MIOS32_USB_CB_SetEndPointFeature,
  NOP_Process, // MIOS32_USB_CB_SetDeviceFeature,
  MIOS32_USB_CB_SetDeviceAddress
};
#endif

/////////////////////////////////////////////////////////////////////////////
// Local Variables
/////////////////////////////////////////////////////////////////////////////

#if 0
// USB Device informations
static DEVICE_INFO My_Device_Info;

// USB device status
static vu32 bDeviceState = UNCONNECTED;
#endif


/////////////////////////////////////////////////////////////////////////////
//! Initializes USB interface
//! \param[in] mode
//!   <UL>
//!     <LI>if 0, USB peripheral won't be initialized if this has already been done before
//!     <LI>if 1, USB peripheral re-initialisation will be forced
//!     <LI>if 2, USB peripheral re-initialisation will be forced, STM32 driver hooks won't be overwritten.<BR>
//!         This mode can be used for a local USB driver which installs it's own hooks during runtime.<BR>
//!         The application can switch back to MIOS32 drivers (e.g. MIOS32_USB_MIDI) by calling MIOS32_USB_Init(1)
//!   </UL>
//! \return < 0 if initialisation failed
//! \note Applications shouldn't call this function directly, instead please use \ref MIOS32_COM or \ref MIOS32_MIDI layer functions
/////////////////////////////////////////////////////////////////////////////
s32 MIOS32_USB_Init(u32 mode)
{
  // currently only mode 0..2 supported
  if( mode >= 3 )
    return -1; // unsupported mode

  // clear all USB interrupt requests
#if 0
  MIOS32_IRQ_Disable();
  //_SetCNTR(0); // Interrupt Mask
  MIOS32_IRQ_Enable();
#endif

#if 0
  // if mode != 2: install MIOS32 hooks
  // a local driver can install it's own hooks and call MIOS32_USB_Init(2) to force re-enumeration
  if( mode != 2 ) {
#ifndef MIOS32_DONT_USE_USB_MIDI
    pEpInt_IN[0]  = MIOS32_USB_MIDI_EP1_IN_Callback;  // IN  EP1
    pEpInt_OUT[0] = MIOS32_USB_MIDI_EP1_OUT_Callback; // OUT EP1
#endif

#ifdef MIOS32_USE_USB_COM
    pEpInt_IN[3]  = MIOS32_USB_COM_EP4_IN_Callback;  // IN  EP4
    pEpInt_OUT[2] = MIOS32_USB_COM_EP3_OUT_Callback; // OUT EP3
#endif
  }
#endif

  // initialise stack
  USBInit();

  // register descriptors
  USBRegisterDescriptors(MIOS32_USB_ConfigDescriptor);

  // register class request handler
  USBRegisterRequestHandler(REQTYPE_TYPE_CLASS, HandleClassRequest, abClassReqData);

  // register endpoint handlers
  //USBHwRegisterEPIntHandler(0x81, NULL);
#ifndef MIOS32_DONT_USE_USB_MIDI
  USBHwRegisterEPIntHandler(0x81, MIOS32_USB_MIDI_EP1_IN_Callback); // (dummy callback)
  // note: shared callback, IN and OUT irq will trigger MIOS32_USB_MIDI_EP1_OUT_Callback
  USBHwRegisterEPIntHandler(0x01, MIOS32_USB_MIDI_EP1_OUT_Callback);
#endif
        
  // register frame handler
  USBHwRegisterFrameHandler(USBFrameHandler);

  // enable bulk-in interrupts on NAKs
  USBHwNakIntEnable(INACK_BI);

  // change connection state to disconnected
#ifndef MIOS32_DONT_USE_USB_MIDI
  MIOS32_USB_MIDI_ChangeConnectionState(0);
  // TODO!
  MIOS32_USB_MIDI_ChangeConnectionState(1);
#endif
#ifdef MIOS32_USE_USB_COM
  MIOS32_USB_COM_ChangeConnectionState(0);
#endif

  // enable_USB_interrupt
  MIOS32_IRQ_Install(USB_IRQn, MIOS32_IRQ_USB_PRIORITY);

  // connect to USB bus
  USBHwConnect(TRUE);

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
//! Interrupt handler for USB
//! \note Applications shouldn't call this function directly, instead please use \ref MIOS32_COM or \ref MIOS32_MIDI layer functions
/////////////////////////////////////////////////////////////////////////////
void USB_IRQHandler(void)
{
  USBHwISR();
}


/////////////////////////////////////////////////////////////////////////////
//! Allows to query, if the USB interface has already been initialized.<BR>
//! This function is used by the bootloader to avoid a reconnection, it isn't
//! relevant for typical applications!
//! \return 1 if USB already initialized, 0 if not initialized
/////////////////////////////////////////////////////////////////////////////
s32 MIOS32_USB_IsInitialized(void)
{
  // USB taken as initialized if clock enabled (reset value is 0)
  return (LPC_USB->USBClkCtrl & (1 << 1)) ? 1 : 0;
}


static BOOL HandleClassRequest(TSetupPacket *pSetup, int *piLen, U8 **ppbData)
{
  switch (pSetup->bRequest) {
  // set line coding
  case SET_LINE_CODING:
    memcpy((U8 *)&LineCoding, *ppbData, 7);
    *piLen = 7;
    break;

  // get line coding
  case GET_LINE_CODING:
    *ppbData = (U8 *)&LineCoding;
    *piLen = 7;
    break;

  // set control line state
  case SET_CONTROL_LINE_STATE:
    // bit0 = DTR, bit = RTS
    break;

  default:
    return FALSE;
  }

  return TRUE;
}

static void USBFrameHandler(U16 wFrame)
{
  // nothing to do...
}


//! \}

#endif /* MIOS32_DONT_USE_USB */
