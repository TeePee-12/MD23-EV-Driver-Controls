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

// Include files
#include <msp430x24x.h>
#include <signal.h>
#include "tri86.h"
#include "can.h"
#include "usci.h"
#include "pedal.h"
#include "gauge.h"
#include "switch.h"
#include "variant.h"
#include "buildnum.h"
#include "flash.h"

// Function prototypes
void clock_init( void );
void io_init( void );
void timerA_init( void );
void timerB_init( void );
void adc_init( void );

// Global variables
// Status and event flags
volatile unsigned int events = 0x0000;
//Copy of flash.can_id. Loaded only at startup -- So if CAN address changed, only used after save&reboot
unsigned int can_addr;

// Data from motor controller
float motor_rpm = 0;
float motor_temp = 0;
float controller_temp = 0;
float battery_voltage = 0;
float battery_current = 0;

//For detecting CAN devices in BL (This is required to ignore another device whilst it is being programmed)
BOOL inblWS = FALSE;

// Main routine
int main( void )
{ 
	// Local variables
	// State machine variables
	unsigned char next_state = MODE_OFF;
	unsigned char current_egear = EG_STATE_NEUTRAL;
	// Comms
	unsigned int comms_event_count = 0;
	// LED flashing
	unsigned char charge_flash_count = CHARGE_FLASH_SPEED;
	
	// Stop watchdog timer
	WDTCTL = WDTPW + WDTHOLD;

	// Initialise I/O ports
	io_init();

	// Initialise clock module - internal osciallator
	clock_init();

	// Fetch hardware info
	variant_retreive_hware();

	// Set up flash timing
	flash_init();

	// Read in constants from flash
	flash_read((unsigned char *)(&flash), sizeof(flash));

	// Check if flash has been programmed, if not, switch to calibration mode - this will send raw ADC counts
	if ((flash.serial == 0xFFFFFFFF) || (flash.serial == 0x00000000) || (flash.can_id > 0x07E0))
	{
		can_addr = flash.can_id = DC_CAN_BASE;
		flash.can_bitrate = CAN_BITRATE_500;
	}
	else
	{
		can_addr = flash.can_id;
	}

	// Initialise SPI port for CAN controller (running with SMCLK)
	usci_init(0);
	
	// Reset CAN controller and initialise
	// This also changes the clock output from the MCP2515, but we're not using it in this software
	can_init( flash.can_bitrate );
	EVENT_CONNECTED_SET;

	// Initialise Timer A (10ms timing ticks)
	timerA_init();

	// Initialise Timer B (gauge outputs PWM / pulses)
	timerB_init();
  
	// Initialise A/D converter for potentiometer and current sense inputs
	adc_init();

	// Initialise switch positions
	switch_init( &switches );
	
	// Initialise command state
	command.rpm = 0.0;
	command.current = 0.0;
	command.bus_current = 1.0;
	command.flags = 0x00;
	command.state = MODE_OFF;
	
	// Init gauges
	gauge_init();

	// Enable interrupts
	eint();

	// Check switch inputs and generate command packets to motor controller
	while (TRUE)
	{
		// Process CAN transmit queue
		can_transmit();

		// Trigger ADC conversions (100Hz)
		if ( EVENT_TIMER_ACTIVE )
		{
			// Clear flag
			EVENT_TIMER_CLR;			
			// Start A/D conversions
			ADC12CTL0 |= ADC12SC;
		}

		// ADC conversion complete, process results
		if ( EVENT_ADC_ACTIVE )
		{
			// Clear flag
			EVENT_ADC_CLR;
			// Check for 5V pedal supply errors
			// TODO
			// Check for overcurrent errors on 12V outputs
			// TODO
			// Update motor commands based on pedal and slider positions
#ifdef REGEN_ON_BRAKE
			process_pedal( ADC12MEM0, ADC12MEM1, ADC12MEM2, (switches & SW_BRAKE) );	// Request regen on brake switch
#else
			process_pedal( ADC12MEM0, ADC12MEM1, ADC12MEM2, FALSE );					// No regen
#endif			
			// Update current state of the switch inputs
			switch_update( &switches );
			
			// Track current operating state
			switch(command.state)
			{
				case MODE_OFF:
					if (switches & SW_IGN_ON) next_state = MODE_N;
					else next_state = MODE_OFF;
					P5OUT &= ~(LED_GEAR_ALL);
					break;
				case MODE_N:
#ifndef USE_EGEAR
					if ((switches & SW_MODE_R) && ((EVENT_SLOW_ACTIVE) || (EVENT_REVERSE_ACTIVE))) next_state = MODE_R;
					else if ((switches & SW_MODE_B) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_BL;
					else if ((switches & SW_MODE_D) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_DL;
#else
					if ((switches & SW_MODE_R) && ((EVENT_SLOW_ACTIVE) || (EVENT_REVERSE_ACTIVE))) next_state = MODE_CO_R;
					else if ( (switches & SW_MODE_B) && ( (EVENT_SLOW_ACTIVE) || (!(EVENT_OVER_VEL_LTOH_ACTIVE) && (EVENT_FORWARD_ACTIVE)) ) ) next_state = MODE_CO_BL;
					else if ( (switches & SW_MODE_B) && ( ((EVENT_OVER_VEL_HTOL_ACTIVE) && (EVENT_FORWARD_ACTIVE)) ) ) next_state = MODE_CO_BH;
					else if ( (switches & SW_MODE_D) && ( (EVENT_SLOW_ACTIVE) || (!(EVENT_OVER_VEL_LTOH_ACTIVE) && (EVENT_FORWARD_ACTIVE)) ) ) next_state = MODE_CO_DL;
					else if ( (switches & SW_MODE_D) && ( ((EVENT_OVER_VEL_HTOL_ACTIVE) && (EVENT_FORWARD_ACTIVE)) ) ) next_state = MODE_CO_DH;
#endif
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else if (switches & SW_FUEL) next_state = MODE_CHARGE;
					else next_state = MODE_N;
					P5OUT &= ~(LED_GEAR_ALL);
					P5OUT |= LED_GEAR_3;
					break;
				case MODE_CO_R:
				case MODE_CO_BL:
				case MODE_CO_BH:
				case MODE_CO_DL:
				case MODE_CO_DH:
					if (switches & SW_MODE_N) next_state = MODE_N;
					else if ((command.state == MODE_CO_R) && (current_egear == EG_STATE_LOW)) next_state = MODE_R;
					else if ((command.state == MODE_CO_BL) && (current_egear == EG_STATE_LOW)) next_state = MODE_BL;
					else if ((command.state == MODE_CO_BH) && (current_egear == EG_STATE_HIGH)) next_state = MODE_BH;
					else if ((command.state == MODE_CO_DL) && (current_egear == EG_STATE_LOW)) next_state = MODE_DL;
					else if ((command.state == MODE_CO_DH) && (current_egear == EG_STATE_HIGH)) next_state = MODE_DH;
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else if (switches & SW_FUEL) next_state = MODE_CHARGE;
					else next_state = command.state;
					break;
				case MODE_R:
					if (switches & SW_MODE_N) next_state = MODE_N;
					else if ((switches & SW_MODE_B) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_BL;	// Assume already in low egear
					else if ((switches & SW_MODE_D) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_DL;	// Assume already in low egear
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else if (switches & SW_FUEL) next_state = MODE_CHARGE;
					else next_state = MODE_R;
					P5OUT &= ~(LED_GEAR_ALL);
					P5OUT |= LED_GEAR_4;
					break;
				case MODE_BL:
					if (switches & SW_MODE_N) next_state = MODE_N;
					else if ((switches & SW_MODE_R) && ((EVENT_SLOW_ACTIVE) || (EVENT_REVERSE_ACTIVE))) next_state = MODE_R;		// Assume already in low egear
					else if ((switches & SW_MODE_D) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_DL;	// Assume already in low egear
#ifdef USE_EGEAR
					else if (EVENT_OVER_VEL_LTOH_ACTIVE) next_state = MODE_CO_BH;
#endif
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else if (switches & SW_FUEL) next_state = MODE_CHARGE;
					else next_state = MODE_BL;
					P5OUT &= ~(LED_GEAR_ALL);
					P5OUT |= LED_GEAR_2;
					break;
				case MODE_BH:
					if (switches & SW_MODE_N) next_state = MODE_N;
					else if ((switches & SW_MODE_D) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_DH;
#ifdef USE_EGEAR
					else if (!(EVENT_OVER_VEL_HTOL_ACTIVE)) next_state = MODE_CO_BL;
#endif
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else if (switches & SW_FUEL) next_state = MODE_CHARGE;
					else next_state = MODE_BH;
					P5OUT &= ~(LED_GEAR_ALL);
					P5OUT |= LED_GEAR_2;
					break;
				case MODE_DL:
					if (switches & SW_MODE_N) next_state = MODE_N;
					else if ((switches & SW_MODE_B) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_BL;	// Assume already in low egear
					else if ((switches & SW_MODE_R) && ((EVENT_SLOW_ACTIVE) || (EVENT_REVERSE_ACTIVE))) next_state = MODE_R;		// Assume already in low egear
#ifdef USE_EGEAR
					else if (EVENT_OVER_VEL_LTOH_ACTIVE) next_state = MODE_CO_DH;
#endif
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else if (switches & SW_FUEL) next_state = MODE_CHARGE;
					else next_state = MODE_DL;
					P5OUT &= ~(LED_GEAR_ALL);
					P5OUT |= LED_GEAR_1;
					break;
				case MODE_DH:
					if (switches & SW_MODE_N) next_state = MODE_N;
					else if ((switches & SW_MODE_B) && ((EVENT_SLOW_ACTIVE) || (EVENT_FORWARD_ACTIVE))) next_state = MODE_BH;
#ifdef USE_EGEAR
					else if (!(EVENT_OVER_VEL_HTOL_ACTIVE)) next_state = MODE_CO_DL;
#endif
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else if (switches & SW_FUEL) next_state = MODE_CHARGE;
					else next_state = MODE_DH;
					P5OUT &= ~(LED_GEAR_ALL);
					P5OUT |= LED_GEAR_1;
					break;
				case MODE_CHARGE:
					if (!(switches & SW_FUEL)) next_state = MODE_N;
					else if (!(switches & SW_IGN_ON)) next_state = MODE_OFF;
					else next_state = MODE_CHARGE;
					// Flash N LED in charge mode
					charge_flash_count--;
					P5OUT &= ~(LED_GEAR_4 | LED_GEAR_2 | LED_GEAR_1);
					if (charge_flash_count == 0)
					{
						charge_flash_count = (CHARGE_FLASH_SPEED * 2);
						P5OUT |= LED_GEAR_3;
					}
					else if (charge_flash_count == CHARGE_FLASH_SPEED)
					{
						P5OUT &= ~LED_GEAR_3;
					}
					break;
				default:
					next_state = MODE_OFF;
					break;
			}
			command.state = next_state;
			
			// Control brake lights
			if ((switches & SW_BRAKE) || (EVENT_REGEN_ACTIVE)) P1OUT |= BRAKE_OUT;
			else P1OUT &= ~BRAKE_OUT;
			
			// Control reversing lights
			if (command.state == MODE_R) P1OUT |= REVERSE_OUT;
			else P1OUT &= ~REVERSE_OUT;
			
			// Control CAN bus power
			if ((switches & SW_IGN_ACC) || (switches & SW_IGN_ON) || (switches & SW_FUEL))
			{
				P1OUT |= CAN_PWR_OUT;
			}
			else
			{
				P1OUT &= ~CAN_PWR_OUT;
				EVENT_CONNECTED_CLR;
			}

			// Control accelerator pedal sense power
			if ((switches & SW_IGN_ACC) || (switches & SW_IGN_ON)) P6OUT |= ANLG_V_ENABLE;
			else P6OUT &= ~ANLG_V_ENABLE;

			// Control gear switch backlighting
			if ((switches & SW_IGN_ACC) || (switches & SW_IGN_ON)) P5OUT |= LED_GEAR_BL;
			else P5OUT &= ~LED_GEAR_BL;
			
			// Control front panel fault indicator
			if (switches & (SW_ACCEL_FAULT | SW_CAN_FAULT | SW_BRAKE_FAULT | SW_REV_FAULT)) P3OUT &= ~LED_REDn;
			else P3OUT |= LED_REDn;			
		}

		// Handle outgoing communications events
		if (EVENT_COMMS_ACTIVE)
		{
			// Clear flag
			EVENT_COMMS_CLR;
			// Blink CAN activity LED
			EVENT_CAN_ACTIVITY_SET;
			// Update command state and override pedal commands if necessary
			if (switches & SW_IGN_ON)
			{
				switch (command.state)
				{
					case MODE_R:
					case MODE_DL:
					case MODE_DH:
					case MODE_BL:
					case MODE_BH:
#ifndef REGEN_ON_BRAKE
#ifdef CUTOUT_ON_BRAKE
						if (switches & SW_BRAKE)
						{
							command.current = 0.0;	
							command.rpm = 0.0;
						}
#endif
#endif
						break;
					case MODE_CHARGE:
					case MODE_N:
					case MODE_START:
					case MODE_OFF:
					case MODE_ON:
					case MODE_CO_R:
					case MODE_CO_DL:
					case MODE_CO_DH:
					case MODE_CO_BL:
					case MODE_CO_BH:
					default:
						command.current = 0.0;
						command.rpm = 0.0;
						break;
				}
			}
			else
			{
				command.current = 0.0;
				command.rpm = 0.0;
			}

			// Transmit commands and telemetry
			if (EVENT_CONNECTED_ACTIVE)
			{
				// Transmit drive command frame
				can_push_ptr->address = can_addr + DC_DRIVE;
				can_push_ptr->status = 8;
				can_push_ptr->data.data_fp[1] = command.current;
				can_push_ptr->data.data_fp[0] = command.rpm;
				can_push();		
	
				// Transmit bus command frame
				can_push_ptr->address = can_addr + DC_POWER;
				can_push_ptr->status = 8;
				can_push_ptr->data.data_fp[1] = command.bus_current;
				can_push_ptr->data.data_fp[0] = 0.0;
				can_push();
				
				// Transmit switch position/activity frame and clear switch differences variables
				can_push_ptr->address = can_addr + DC_SWITCH;
				can_push_ptr->status = 8;
				can_push_ptr->data.data_u8[7] = command.state;
				can_push_ptr->data.data_u8[6] = command.flags;
				can_push_ptr->data.data_u16[2] = 0;
				can_push_ptr->data.data_u16[1] = 0;
				can_push_ptr->data.data_u16[0] = switches;
				can_push();

				// Transmit egear control packet if needed
#ifdef USE_EGEAR
				if (		(command.state == MODE_CO_R && next_state == MODE_CO_R)
					||	(command.state == MODE_CO_BL && next_state == MODE_CO_BL)
					||	(command.state == MODE_CO_BH && next_state == MODE_CO_BH)
					||	(command.state == MODE_CO_DL && next_state == MODE_CO_DL)
					||	(command.state == MODE_CO_DH && next_state == MODE_CO_DH))
				{
					if ( current_egear == EG_STATE_NEUTRAL)
					{
						can_push_ptr->address = EG_CAN_BASE + EG_COMMAND;
						can_push_ptr->status = 8;
						can_push_ptr->data.data_u32[0] = 0;
						can_push_ptr->data.data_u32[1] = 0;
						if (command.state == MODE_CO_R) can_push_ptr->data.data_u8[0] = EG_CMD_LOW;
						else if ( command.state == MODE_CO_BL) can_push_ptr->data.data_u8[0] = EG_CMD_LOW;
						else if ( command.state == MODE_CO_DL) can_push_ptr->data.data_u8[0] = EG_CMD_LOW;
						else if ( command.state == MODE_CO_BH) can_push_ptr->data.data_u8[0] = EG_CMD_HIGH;
						else if ( command.state == MODE_CO_DH) can_push_ptr->data.data_u8[0] = EG_CMD_HIGH;
						can_push();
					}
					else if (EVENT_MC_NEUTRAL_ACTIVE)
					{
						can_push_ptr->address = EG_CAN_BASE + EG_COMMAND;
						can_push_ptr->status = 8;
						can_push_ptr->data.data_u32[0] = 0;
						can_push_ptr->data.data_u32[1] = 0;
						can_push_ptr->data.data_u8[0] = EG_CMD_NEUTRAL;
						can_push();
					}
				}
				else if (command.state == MODE_N)
				{
					can_push_ptr->address = EG_CAN_BASE + EG_COMMAND;
					can_push_ptr->status = 8;
					can_push_ptr->data.data_u32[0] = 0;
					can_push_ptr->data.data_u32[1] = 0;
					can_push_ptr->data.data_u8[0] = EG_CMD_NEUTRAL;
					can_push();
				}
				else if ((command.state == MODE_BL) || (command.state == MODE_DL) || (command.state == MODE_R))
				{
					can_push_ptr->address = EG_CAN_BASE + EG_COMMAND;
					can_push_ptr->status = 8;
					can_push_ptr->data.data_u32[0] = 0;
					can_push_ptr->data.data_u32[1] = 0;
					can_push_ptr->data.data_u8[0] = EG_CMD_LOW;
					can_push();
				}
				else if ((command.state == MODE_BH) || (command.state == MODE_DH))
				{
					can_push_ptr->address = EG_CAN_BASE + EG_COMMAND;
					can_push_ptr->status = 8;
					can_push_ptr->data.data_u32[0] = 0;
					can_push_ptr->data.data_u32[1] = 0;
					can_push_ptr->data.data_u8[0] = EG_CMD_HIGH;
					can_push();
				}
#endif
				
				// Transmit our ID frame at a slower rate (every 10 events = 1/second)
				comms_event_count++;
				if (comms_event_count == 10)
				{
					comms_event_count = 0;
					can_push_ptr->address = can_addr;
					can_push_ptr->status = 8;
					//NB: THESE WERE THE WRONG WAY... ALL TRITIUM DEVICES MUST HAVE DEVICE_ID IN LOWER 32BIT VALUE
					can_push_ptr->data.data_u32[0] = hware.device_id;		// Device ID 
					can_push_ptr->data.data_u32[1] = flash.serial;			// Serial number
					can_push();		
				}
			}
		}

		// Check for CAN packet reception
		if ((P2IN & CAN_INTn) == 0x00)
		{
			// IRQ flag is set, so run the receive routine to either get the message, or the error
			can_receive();
			// Check the status
			if (can.status == CAN_OK)
			{
				// We've received a packet, so must be connected to something
				EVENT_CONNECTED_SET;
				// Process the packet
				if (can.address == (can_addr + DC_BOOTLOAD) && (hware.bootloader_version < NEWBL_JUMP_MIN_VER))
				{
					//OLD BOOTLOADER JUMP MECHANISM - LEGACY SUPPORT ONLY
					if (		can.data.data_u8[0] == 'B' && can.data.data_u8[1] == 'O' && can.data.data_u8[2] == 'O' && can.data.data_u8[3] == 'T'
							&&	can.data.data_u8[4] == 'L' && can.data.data_u8[5] == 'O' && can.data.data_u8[6] == 'A' && can.data.data_u8[7] == 'D' )
					{
						WDTCTL = 0x00;	// Force watchdog reset
					}
				}
				else if ((can.address == can_addr) && (can.data.data_u32[1] == flash.serial))
				{
					//TRITIUM PRODUCT VERSION REPORTING SYSTEM COMMAND
					if (variant_devinfo_req(can.data.data_u8[3]) == DEV_INFO_REQ_BLOAD)
					{
						//NEW BOOTLOADER JUMP MECHANISM - Allow jump even if old BL.. It will work
						//Jump to bootloader - MUST DISABLE INTERRUPTS and turn off PWM/ADC peripherals from pins
						dint();
						// Disable used peripheral interrupts (If new user code enables master interrupts these may trip an erroneous interrupt call if not implemented)
						ADC12IE = 0;
						TACCTL0 = 0;								
						TBCCTL0 = 0;
						//BL requires that the WD flag in IFG1 is cleared
						__asm__ __volatile__ ("bic.b %0,&__IFG1" : : "i" (WDTIFG));
						//Jump to BL - NB: Using reset vector address to get start address of BL
						__asm__ __volatile__ ("br &0xFFFE");
					}
				}
				else if (can.address == (MC_CAN_BASE + MC_VELOCITY))
				{
					// Update speed threshold event flags
					if (can.data.data_fp[0] > ENGAGE_VEL_F) EVENT_FORWARD_SET;
					else EVENT_FORWARD_CLR;
					if (can.data.data_fp[0] < ENGAGE_VEL_R) EVENT_REVERSE_SET;
					else EVENT_REVERSE_CLR;
					if ((can.data.data_fp[0] >= ENGAGE_VEL_R) && (can.data.data_fp[0] <= ENGAGE_VEL_F)) EVENT_SLOW_SET;
					else EVENT_SLOW_CLR;
					if (can.data.data_fp[0] >= CHANGE_VEL_LTOH) EVENT_OVER_VEL_LTOH_SET;
					else EVENT_OVER_VEL_LTOH_CLR;
					if (can.data.data_fp[0] >= CHANGE_VEL_HTOL) EVENT_OVER_VEL_HTOL_SET;
					else EVENT_OVER_VEL_HTOL_CLR;
					motor_rpm = can.data.data_fp[0];
					gauge_tach_update( motor_rpm );
				}
				else if (can.address == (MC_CAN_BASE + MC_I_VECTOR))
				{
					// Update regen status flags
					if (can.data.data_fp[0] < REGEN_THRESHOLD) EVENT_REGEN_SET;
					else EVENT_REGEN_CLR;
				}
				else if (can.address == (MC_CAN_BASE + MC_TEMP1))
				{
					// Update data for temp gauge
					controller_temp = can.data.data_fp[1];
					motor_temp = can.data.data_fp[0];
					gauge_temp_update( motor_temp, controller_temp );
				}
				else if (can.address == MC_CAN_BASE)
				{
					//Must detect if WS is in bootload and ignore CAN id's (MC_BASE +1 & +2) until programming has completed
					inblWS = (can.data.data_u8[3] == 0xFF);
				}
				else if (can.address == (MC_CAN_BASE + MC_LIMITS) && !inblWS)
				{
					// Update neutral state of motor controller
					if (can.data.data_u8[0] == 0) EVENT_MC_NEUTRAL_SET;
					else EVENT_MC_NEUTRAL_CLR;
				}
				else if (can.address == (MC_CAN_BASE + MC_BUS) && !inblWS)
				{
					// Update battery voltage and current for fuel and power gauges
					battery_voltage = can.data.data_fp[0];
					battery_current = can.data.data_fp[1];
					gauge_power_update( battery_voltage, battery_current );
					gauge_fuel_update( battery_voltage );
				}
				else if (can.address == (EG_CAN_BASE + EG_STATUS))
				{
					if ( can.data.data_u8[0] == EG_STATE_NEUTRAL ) current_egear = EG_STATE_NEUTRAL;
					else if ( can.data.data_u8[0] == EG_STATE_LOW ) current_egear = EG_STATE_LOW;
					else if ( can.data.data_u8[0] == EG_STATE_HIGH ) current_egear = EG_STATE_HIGH;
				}
				// Check for calibration / setup messages
				else if (can.address == (can_addr + DC_SETUP))
				{
					// Write data command
					if (can.data.data_u8[6] == FLASH_CMD_WRITE)
					{
						switch(can.data.data_u8[7])
						{
							case FLASH_SERIAL:
								flash.serial = can.data.data_u32[0];
								break;
							case FLASH_CAN_ID:
								//Ensure the CAN ID is valid - otherwise may not be able to communicate with device correctly
								//Default assumed range of IDs is 0x20. If more ID range is required then lower maximum ID check appropriately
								if (!(can.data.data_u16[0] % 0x20) && (can.data.data_u16[0] <= 0x07E0)) flash.can_id = can.data.data_u16[0];
								break;
							case FLASH_CAN_BITRATE:
								flash.can_bitrate = can.data.data_u16[0];
								break;
							case FLASH_WRITE_TRIGGER:
								dint();
								flash_erase();
								flash_write((unsigned char *)(&flash), sizeof(flash));
								eint();
								// Force a reset of the part, to come back up in operating mode
								WDTCTL = 0x00;	
								break;
							default:
								break;
						}
					}
					// Read data request
					else if (can.data.data_u8[6] == FLASH_CMD_READ)
					{
						//Clear all data so dont have any ugly left over data send allong with requested value
						can_push_ptr->data.data_u64 = 0;
						switch(can.data.data_u8[7])
						{
							case FLASH_SERIAL:
								can_push_ptr->data.data_u32[0] = flash.serial;
								break;
							case FLASH_CAN_ID:
								can_push_ptr->data.data_u16[0] = flash.can_id;
								break;
							case FLASH_CAN_BITRATE:
								can_push_ptr->data.data_u16[0] = flash.can_bitrate;
								break;
							default:
								can_push_ptr->data.data_u32[0] = 0x00000000;
								break;
						}
						can_push_ptr->address = (can_addr + DC_SETUP);
						can_push_ptr->status = 8;
						can_push_ptr->data.data_u8[7] = can.data.data_u8[7];
						can_push_ptr->data.data_u8[6] = FLASH_CMD_REPLY;
						can_push();
					}
				}
			}
			else if (can.status == CAN_RTR)
			{
				// Remote request packet received - reply to it
				if (can.address == can_addr)
				{
					can_push_ptr->address = can.address;
					can_push_ptr->status = 8;
					//NB: THESE WERE THE WRONG WAY... ALL TRITIUM DEVICES MUST HAVE DEVICE_ID IN LOWER 32BIT VALUE
					can_push_ptr->data.data_u32[0] = hware.device_id;	// Device ID
					can_push_ptr->data.data_u32[1] = flash.serial;		// Serial number
					can_push();
				}
				else if (can.address == (can_addr + DC_DRIVE))
				{
					can_push_ptr->address = can.address;
					can_push_ptr->status = 8;
					can_push_ptr->data.data_fp[1] = command.current;
					can_push_ptr->data.data_fp[0] = command.rpm;
					can_push();
				}
				else if (can.address == (can_addr + DC_POWER))
				{
					can_push_ptr->address = can.address;
					can_push_ptr->status = 8;
					can_push_ptr->data.data_fp[1] = command.bus_current;
					can_push_ptr->data.data_fp[0] = 0.0;
					can_push();
				}
				else if (can.address == (can_addr + DC_SWITCH))
				{
					can_push_ptr->address = can.address;
					can_push_ptr->status = 8;
					can_push_ptr->data.data_u8[7] = command.state;
					can_push_ptr->data.data_u8[6] = command.flags;
					can_push_ptr->data.data_u16[2] = 0;
					can_push_ptr->data.data_u16[1] = 0;
					can_push_ptr->data.data_u16[0] = switches;
					can_push();
				}
				//LEGACY SUPPORT ONLY
				else if (can.address == (can_addr + DC_INFO))
				{
					can_push_ptr->address = can.address;
					can_push_ptr->status = 8;
					can_push_ptr->data.data_u16[0] = BUILD_NUMBER;
					can_push_ptr->data.data_u8[2] = hware.hardware_version;
					can_push_ptr->data.data_u8[3] = hware.model_id;
					can_push_ptr->data.data_u8[4] = hware.bootloader_version;
//					can_push_ptr->data.data_u8[5] = can_tx_err_count;
//					can_push_ptr->data.data_u8[6] = can_rx_err_count;
					can_push_ptr->data.data_u8[5] = 0x00;
					can_push_ptr->data.data_u8[6] = 0x00;
					can_push_ptr->data.data_u8[7] = 0x00;
					can_push();
				}
			}
			//else if (can.status == CAN_ERROR){
			//}
		}
	}
	
	// Will never get here, keeps compiler happy
	return(1);
}

/*
 * Initialise clock module
 *	- Setup MCLK, ACLK, SMCLK dividers and clock sources
 *	- ACLK  = 0
 *	- MCLK  = 16 MHz internal oscillator
 *	- SMCLK = 16 MHz internal oscillator
 *
 * Note: We can also use the 2, 4, 8 or 16MHz crystal clock output from the MCP2515 CAN controller, which
 *       is a more accurate source than the internal oscillator.  However, using the internal
 *       source makes using sleep modes for both the MSP430 and the MCP2515 much simpler.
 */
void clock_init( void )
{
	BCSCTL1 = CALBC1_16MHZ;
	DCOCTL = CALDCO_16MHZ;
}

/*
 * Initialise I/O port directions and states
 *	- Drive unused pins as outputs to avoid floating inputs
 *
 */
void io_init( void )
{
	P1OUT = 0x00;
	P1DIR = BRAKE_OUT | REVERSE_OUT | CAN_PWR_OUT | P1_UNUSED;
	
	P2OUT = 0x00;
	P2DIR = P2_UNUSED;
	
	P3OUT = CAN_CSn | EXPANSION_TXD | LED_REDn | LED_GREENn;
	P3DIR = CAN_CSn | CAN_MOSI | CAN_SCLK | EXPANSION_TXD | LED_REDn | LED_GREENn | P3_UNUSED;
	
	P4OUT = LED_PWM;
	P4DIR = GAUGE_1_OUT | GAUGE_2_OUT | GAUGE_3_OUT | GAUGE_4_OUT | LED_PWM | P4_UNUSED;
	
	P5OUT = 0x00;
	P5DIR = LED_FAULT_1 | LED_FAULT_2 | LED_FAULT_3 | LED_GEAR_BL | LED_GEAR_4 | LED_GEAR_3 | LED_GEAR_2 | LED_GEAR_1 | P5_UNUSED;
	
	P6OUT = 0x00;
	P6DIR = ANLG_V_ENABLE | P6_UNUSED;
}


/*
 * Initialise Timer A
 *	- Provides timer tick timebase at 100 Hz
 */
void timerA_init( void )
{
	TACTL = TASSEL_2 | ID_3 | TACLR;			// MCLK/8, clear TAR
	TACCR0 = (INPUT_CLOCK/8/TICK_RATE);			// Set timer to count to this value = TICK_RATE overflow
	TACCTL0 = CCIE;								// Enable CCR0 interrrupt
	TACTL |= MC_1;								// Set timer to 'up' count mode
}


/*
 * Initialise Timer B
 *	- Provides PWM and pulse outputs for gauges
 *	- 10000Hz timer ISR
 *	- With 16MHz clock/8 = 200 count resolution on PWM
 *
 */
void timerB_init( void )
{
	TBCTL = TBSSEL_2 | ID_3 | TBCLR;			// MCLK/8, clear TBR
	TBCCR0 = GAUGE_PWM_PERIOD;					// Set timer to count to this value
	TBCCR3 = 0;									// Gauge 3
	TBCCTL3 = OUTMOD_7;
	TBCCR4 = 0;									// Gauge 4
	TBCCTL4 = OUTMOD_7;
	P4SEL |= GAUGE_3_OUT | GAUGE_4_OUT;			// PWM -> output pins for fuel and temp gauges (tacho and power are software freq outputs)
	TBCCTL0 = CCIE;								// Enable CCR0 interrupt
	TBCTL |= MC_1;								// Set timer to 'up' count mode
}

/*
 * Initialise A/D converter
 */
void adc_init( void )
{
	// Enable A/D input channels											
	P6SEL |= ANLG_SENSE_A | ANLG_SENSE_B | ANLG_SENSE_C | ANLG_SENSE_V | ANLG_BRAKE_I | ANLG_REVERSE_I | ANLG_CAN_PWR_I;
	// Turn on ADC12, set sampling time = 256 ADCCLK, multiple conv, start internal 2.5V reference
	ADC12CTL0 = ADC12ON | SHT0_8 | SHT1_8 | MSC | REFON | REF2_5V;	
	// Use sampling timer, ADCCLK = MCLK/4, run a single sequence per conversion start
	ADC12CTL1 = ADC12SSEL_2 | ADC12DIV_3 | SHP | CONSEQ_1;
	// Map conversion channels to input channels & reference voltages
	ADC12MCTL0 = INCH_3 | SREF_1;			// Analog A
	ADC12MCTL1 = INCH_2 | SREF_1;			// Analog B
	ADC12MCTL2 = INCH_1 | SREF_1;			// Analog C
	ADC12MCTL3 = INCH_4 | SREF_1;			// Analog V Supply
	ADC12MCTL4 = INCH_5 | SREF_1;			// Brake light current
	ADC12MCTL5 = INCH_6 | SREF_1;			// Reverse light current
	ADC12MCTL6 = INCH_7 | SREF_1 | EOS;		// CAN Bus current / End of sequence
	// Enable interrupts on final conversion in sequence
	ADC12IE = BIT6;	
	// Enable conversions
	ADC12CTL0 |= ENC;											
}

/*
 * Timer B CCR0 Interrupt Service Routine
 *	- Interrupts on Timer B CCR0 match at GAUGE_FREQUENCY (10kHz)
 */
interrupt(TIMERB0_VECTOR) timer_b0(void)
{
	static unsigned int gauge_count;
	static unsigned int gauge1_on, gauge1_off;
	static unsigned int gauge2_on, gauge2_off;
	
	// Toggle gauge 1 & 2 pulse frequency outputs
	if (gauge_count == gauge1_on)
	{
		P4OUT |= GAUGE_1_OUT;
		gauge1_on = gauge_count + gauge.g1_count;
		gauge1_off = gauge_count + (gauge.g1_count >> 2);
	}
	if (gauge_count == gauge1_off)
	{
		P4OUT &= ~GAUGE_1_OUT;
	}

	if (gauge_count == gauge2_on)
	{
		P4OUT |= GAUGE_2_OUT;
		gauge2_on = gauge_count + gauge.g2_count;
		gauge2_off = gauge_count + (gauge.g2_count >> 2);
	}
	if (gauge_count == gauge2_off)
	{
		P4OUT &= ~GAUGE_2_OUT;
	}

	// Update pulse output timebase counter
	gauge_count++;
	
	// Update outputs if necessary
	if (EVENT_GAUGE1_ACTIVE)
	{
		EVENT_GAUGE1_CLR;
	}
	if (EVENT_GAUGE2_ACTIVE)
	{
		EVENT_GAUGE2_CLR;
	}
	if (EVENT_GAUGE3_ACTIVE)
	{
		EVENT_GAUGE3_CLR;
		TBCCR3 = gauge.g3_duty;		
	}
	if (EVENT_GAUGE4_ACTIVE)
	{
		EVENT_GAUGE4_CLR;
		TBCCR4 = gauge.g4_duty;		
	}	
}

/*
 * Timer A CCR0 Interrupt Service Routine
 *	- Interrupts on Timer A CCR0 match at 100Hz
 *	- Sets Time_Flag variable
 */
interrupt(TIMERA0_VECTOR) timer_a0(void)
{
	static unsigned char comms_count = COMMS_SPEED;
	static unsigned char activity_count;
	
	// Trigger timer based events
	EVENT_TIMER_SET;
	
	// Trigger comms events (command packet transmission)
	comms_count--;
	if ( comms_count == 0 )
	{
		comms_count = COMMS_SPEED;
		EVENT_COMMS_SET;
	}
	
	// Check for CAN activity events and blink LED
	if (EVENT_CAN_ACTIVITY_ACTIVE)
	{
		EVENT_CAN_ACTIVITY_CLR;
		activity_count = ACTIVITY_SPEED;
		P3OUT &= ~LED_GREENn;
	}
	if ( activity_count == 0 )
	{
		P3OUT |= LED_GREENn;
	}
	else
	{
		activity_count--;
	}
}

/*
 * ADC12 Interrupt Service Routine
 *	- Interrupts on channel 6 conversion (end of sequence)
 */
interrupt(ADC12_VECTOR) adc_isr(void)
{
	// Clear ISR flag
	ADC12IFG &= ~BIT6;
	// Trigger ADC event in main loop
	EVENT_ADC_SET;
}
