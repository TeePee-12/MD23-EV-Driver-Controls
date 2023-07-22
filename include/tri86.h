/*
 * Tritium TRI86 EV Driver Controls, up to v4 hardware
 * Copyright (c) 2015, Tritium Pty Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *	- Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
 *	  in the documentation and/or other materials provided with the distribution.
 *	- Neither the name of Tritium Pty Ltd nor the names of its contributors may be used to endorse or promote products 
 *	  derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */
#ifndef TRI86_H
#define TRI86_H

// Pin Definitions
// Port 1
#define IN_FUEL				0x01
#define IN_BRAKEn			0x02
#define IN_IGN_STARTn		0x04
#define IN_IGN_ONn			0x08
#define IN_IGN_ACCn			0x10
#define BRAKE_OUT			0x20
#define REVERSE_OUT			0x40
#define CAN_PWR_OUT			0x80
#define P1_UNUSED			0x00

// Port 2
#define IN_GEAR_1			0x01
#define IN_GEAR_2			0x02
#define IN_GEAR_3			0x04
#define IN_GEAR_4			0x08
#define IN_GEAR_5			0x10
#define IN_GEAR_6			0x20
#define EXPANSION_IRQ		0x40
#define CAN_INTn			0x80
#define P2_UNUSED			0x00

// Port 3
#define CAN_CSn				0x01
#define CAN_MOSI			0x02
#define CAN_MISO			0x04
#define CAN_SCLK			0x08
#define EXPANSION_TXD		0x10
#define EXPANSION_RXD		0x20
#define LED_REDn			0x40
#define LED_GREENn			0x80
#define P3_UNUSED			0x00

// Port 4
#define EXPANSION_GPIO		0x01
#define GAUGE_4_OUT			0x02
#define GAUGE_3_OUT			0x04
#define GAUGE_2_OUT			0x08
#define GAUGE_1_OUT			0x10
#define LED_PWM				0x20
#define P4_UNUSED			0x40 | 0x80

// Port 5
#define LED_FAULT_3			0x01
#define LED_FAULT_2			0x02
#define LED_FAULT_1			0x04
#define LED_GEAR_BL			0x08
#define LED_GEAR_4			0x10
#define LED_GEAR_3			0x20
#define LED_GEAR_2			0x40
#define LED_GEAR_1			0x80
#define P5_UNUSED			0x00

#define LED_GEAR_ALL		(LED_GEAR_4 | LED_GEAR_3 | LED_GEAR_2 | LED_GEAR_1)

// Port 6
#define ANLG_V_ENABLE		0x01
#define ANLG_SENSE_C		0x02
#define ANLG_SENSE_B		0x04
#define ANLG_SENSE_A		0x08
#define ANLG_SENSE_V		0x10
#define ANLG_BRAKE_I		0x20
#define ANLG_REVERSE_I		0x40
#define ANLG_CAN_PWR_I		0x80
#define P6_UNUSED			0x00

// Compile-time options
// #define USE_EGEAR			// Use series/parallel contactor changeover controller
// #define REGEN_ON_BRAKE		// Use brake pedal to trigger regen, with amount set by Analog C input
// #define CUTOUT_ON_BRAKE		// Cut throttle on brake pedal active (solarcar preference to avoid dragging brakes)
// #define REGEN_SETS_RPM		// Use regen input (Analog C) to set target rpm value

// Device serial number
//#define DEVICE_SERIAL		8070

// Constant Definitions
#define	TRUE				1
#define FALSE				0

// Pushbutton switch states
#define PUSHED				1
#define RELEASED			0

// Drive states
#define MODE_OFF			0
#define MODE_ON				1
#define MODE_START			2
#define MODE_R				3
#define MODE_N				4
#define MODE_BL				5
#define MODE_DL				6
#define MODE_CHARGE			7
#define MODE_BH				8
#define MODE_DH				9
#define MODE_CO_R			10
#define MODE_CO_BL			11
#define MODE_CO_BH			12
#define MODE_CO_DL			13
#define MODE_CO_DH			14

// Event timing
#define INPUT_CLOCK			16000000			// Hz
#define TICK_RATE			100					// Hz, poll switches and update ADC at this rate
#define COMMS_SPEED			10					// Number of ticks per event: 10 ticks = 100ms = 10 Hz
#define CHARGE_FLASH_SPEED	20					// LED flash rate in charge mode: 20 ticks = 200ms = 5 Hz
#define ACTIVITY_SPEED		2					// LED flash period for CAN activity: 2 ticks = 20ms

// Event definitions
#define EVENT_TIMER					0x0001		// Timer went off
#define EVENT_COMMS					0x0002		// Time to transmit telemetry
#define EVENT_REGEN					0x0004		// Motor controller is regenning
#define EVENT_ADC					0x0008		// ADC conversions are complete
#define EVENT_SLOW					0x0010		// Vehicle is within ENGAGE_VEL_R and ENGAGE_VEL_F speeds
#define EVENT_FORWARD				0x0020		// Vehicle is driving above ENGAGE_VEL_F speed
#define EVENT_REVERSE				0x0040		// Vehicle is reversing above ENGAGE_VEL_R speed
#define EVENT_CONNECTED				0x0080		// CAN bus is present and communicating
#define EVENT_CAN_ACTIVITY			0x0100		// CAN controller just transmitted a packet
#define EVENT_MC_NEUTRAL			0x0200		// Motor controller is in neutral
#define EVENT_OVER_VEL_LTOH			0x0400		// Motor speed is above maximum for LOW egear
#define EVENT_OVER_VEL_HTOL			0x0800		// Motor speed is above minimum for HIGH egear
#define EVENT_GAUGE1				0x1000		// Signal that gauge 1 has been recalculated and requires update
#define EVENT_GAUGE2				0x2000		// Signal that gauge 2 has been recalculated and requires update
#define EVENT_GAUGE3				0x4000		// Signal that gauge 3 has been recalculated and requires update
#define EVENT_GAUGE4				0x8000		// Signal that gauge 4 has been recalculated and requires update

#define EVENT_TIMER_SET				( events |= EVENT_TIMER )
#define EVENT_TIMER_CLR				( events &= ~EVENT_TIMER )
#define EVENT_TIMER_ACTIVE			( events & EVENT_TIMER )

#define EVENT_COMMS_SET				( events |= EVENT_COMMS )
#define EVENT_COMMS_CLR				( events &= ~EVENT_COMMS )
#define EVENT_COMMS_ACTIVE			( events & EVENT_COMMS )

#define EVENT_REGEN_SET				( events |= EVENT_REGEN )
#define EVENT_REGEN_CLR				( events &= ~EVENT_REGEN )
#define EVENT_REGEN_ACTIVE			( events & EVENT_REGEN )

#define EVENT_ADC_SET				( events |= EVENT_ADC )
#define EVENT_ADC_CLR				( events &= ~EVENT_ADC )
#define EVENT_ADC_ACTIVE			( events & EVENT_ADC )

#define EVENT_SLOW_SET				( events |= EVENT_SLOW )
#define EVENT_SLOW_CLR				( events &= ~EVENT_SLOW )
#define EVENT_SLOW_ACTIVE			( events & EVENT_SLOW )

#define EVENT_FORWARD_SET			( events |= EVENT_FORWARD )
#define EVENT_FORWARD_CLR			( events &= ~EVENT_FORWARD )
#define EVENT_FORWARD_ACTIVE		( events & EVENT_FORWARD )

#define EVENT_REVERSE_SET			( events |= EVENT_REVERSE )
#define EVENT_REVERSE_CLR			( events &= ~EVENT_REVERSE )
#define EVENT_REVERSE_ACTIVE		( events & EVENT_REVERSE )

#define EVENT_CONNECTED_SET			( events |= EVENT_CONNECTED )
#define EVENT_CONNECTED_CLR			( events &= ~EVENT_CONNECTED )
#define EVENT_CONNECTED_ACTIVE		( events & EVENT_CONNECTED )

#define EVENT_CAN_ACTIVITY_SET		( events |= EVENT_CAN_ACTIVITY )
#define EVENT_CAN_ACTIVITY_CLR		( events &= ~EVENT_CAN_ACTIVITY )
#define EVENT_CAN_ACTIVITY_ACTIVE	( events & EVENT_CAN_ACTIVITY )

#define EVENT_MC_NEUTRAL_SET		( events |= EVENT_MC_NEUTRAL )
#define EVENT_MC_NEUTRAL_CLR		( events &= ~EVENT_MC_NEUTRAL )
#define EVENT_MC_NEUTRAL_ACTIVE		( events & EVENT_MC_NEUTRAL )

#define EVENT_OVER_VEL_LTOH_SET		( events |= EVENT_OVER_VEL_LTOH )
#define EVENT_OVER_VEL_LTOH_CLR		( events &= ~EVENT_OVER_VEL_LTOH )
#define EVENT_OVER_VEL_LTOH_ACTIVE	( events & EVENT_OVER_VEL_LTOH )

#define EVENT_OVER_VEL_HTOL_SET		( events |= EVENT_OVER_VEL_HTOL )
#define EVENT_OVER_VEL_HTOL_CLR		( events &= ~EVENT_OVER_VEL_HTOL )
#define EVENT_OVER_VEL_HTOL_ACTIVE	( events & EVENT_OVER_VEL_HTOL )

#define EVENT_GAUGE1_SET			( events |= EVENT_GAUGE1 )
#define EVENT_GAUGE1_CLR			( events &= ~EVENT_GAUGE1 )
#define EVENT_GAUGE1_ACTIVE			( events & EVENT_GAUGE1 )

#define EVENT_GAUGE2_SET			( events |= EVENT_GAUGE2 )
#define EVENT_GAUGE2_CLR			( events &= ~EVENT_GAUGE2 )
#define EVENT_GAUGE2_ACTIVE			( events & EVENT_GAUGE2 )

#define EVENT_GAUGE3_SET			( events |= EVENT_GAUGE3 )
#define EVENT_GAUGE3_CLR			( events &= ~EVENT_GAUGE3 )
#define EVENT_GAUGE3_ACTIVE			( events & EVENT_GAUGE3 )

#define EVENT_GAUGE4_SET			( events |= EVENT_GAUGE4 )
#define EVENT_GAUGE4_CLR			( events &= ~EVENT_GAUGE4 )
#define EVENT_GAUGE4_ACTIVE			( events & EVENT_GAUGE4 )

// Control parameters
#define ENGAGE_VEL_F		50					// Don't allow drive direction change above this speed, rpm
#define ENGAGE_VEL_R		-50					// Don't allow drive direction change above this speed, rpm
#define REGEN_THRESHOLD		-5					// Brake lights come on above this motor current, A
#define CHANGE_VEL_LTOH		1800				// Shift up (if using egear) above this speed, rpm
#define CHANGE_VEL_HTOL		1600				// Shift down (if using egear) below this speed, rpm

// Public variables
extern volatile unsigned int events;
extern unsigned int can_addr;

//Integer type defs
typedef char				int8;
typedef int             	int16;
typedef long            	int32;
typedef long long			int64;
typedef unsigned char		UInt8;
typedef unsigned int    	UInt16;
typedef unsigned long   	UInt32;
typedef unsigned long long	UInt64;
typedef float				float32;
typedef int                 BOOL;

// Typedefs for quickly joining multiple bytes/ints/etc into larger values
// These rely on byte ordering in CPU & memory - i.e. they're not portable across architectures
typedef union _group_64 {
	UInt64	data_u64;
	int64	data_64;
	float32	data_fp[2];
	UInt8	data_u8[8];
	int8	data_8[8];
	UInt16	data_u16[4];
	int16	data_16[4];
	UInt32	data_u32[2];
	int32	data_32[2];
} group_64;

typedef union _group_32 {
	float32	data_fp;
	UInt8	data_u8[4];
	int8	data_8[4];
	UInt16	data_u16[2];
	int16	data_16[2];
	UInt32	data_u32;
	int32	data_32;
} group_32;

typedef union _group_16 {
	UInt8	data_u8[2];
	int8	data_8[2];
	UInt16	data_u16;
	int16	data_16;
} group_16;

#endif
