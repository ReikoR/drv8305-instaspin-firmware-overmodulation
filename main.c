/* --COPYRIGHT--,BSD
 * Copyright (c) 2012, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --/COPYRIGHT--*/
//! \file   solutions/instaspin_foc/src/proj_lab10a.c
//! \brief Space Vector Over-Modulation
//!
//! (C) Copyright 2011, Texas Instruments, Inc.
//! \defgroup PROJ_LAB10a PROJ_LAB10a
//@{
//! \defgroup PROJ_LAB10a_OVERVIEW Project Overview
//!
//! Experimentation with Space Vector Over-Modulation
//!
// **************************************************************************
// the includes
// system includes
#include <math.h>
#include "main.h"
#include "modules/angle_gen/angle_gen.h"
#include "modules/vs_freq/vs_freq.h"

#ifdef FLASH
#pragma CODE_SECTION(mainISR,"ramfuncs");
#endif

// Include header files used in the main function

// **************************************************************************
// the defines

#define LED_BLINK_FREQ_Hz   5

// **************************************************************************
// the globals

uint_least16_t gCounter_updateGlobals = 0;

bool Flag_Latch_softwareUpdate = true;

CTRL_Handle ctrlHandle;

#ifdef CSM_ENABLE
#pragma DATA_SECTION(halHandle,"rom_accessed_data");
#endif

HAL_Handle halHandle;

#ifdef CSM_ENABLE
#pragma DATA_SECTION(gUserParams,"rom_accessed_data");
#endif

USER_Params gUserParams;

HAL_PwmData_t gPwmData = { _IQ(0.0), _IQ(0.0), _IQ(0.0) };

HAL_AdcData_t gAdcData;

_iq gMaxCurrentSlope = _IQ(0.0);

#ifdef FAST_ROM_V1p6
CTRL_Obj *controller_obj;
#else

#ifdef CSM_ENABLE
#pragma DATA_SECTION(ctrl,"rom_accessed_data");
#endif

CTRL_Obj ctrl;				//v1p7 format
CTRL_Obj *controller_obj;
#endif

uint16_t gLEDcnt = 0;

volatile MOTOR_Vars_t gMotorVars = MOTOR_Vars_INIT;

#ifdef FLASH
// Used for running BackGround in flash, and ISR in RAM
		extern uint16_t *RamfuncsLoadStart,
*RamfuncsLoadEnd, *RamfuncsRunStart;

#ifdef CSM_ENABLE
extern uint16_t *econst_start, *econst_end, *econst_ram_load;
extern uint16_t *switch_start, *switch_end, *switch_ram_load;
#endif
#endif

SVGENCURRENT_Obj svgencurrent;
SVGENCURRENT_Handle svgencurrentHandle;

// set the offset, default value of 1 microsecond
int16_t gCmpOffset = (int16_t) (1.0 * USER_SYSTEM_FREQ_MHz);

MATH_vec3 gIavg = { _IQ(0.0), _IQ(0.0), _IQ(0.0) };
uint16_t gIavg_shift = 1;
MATH_vec3 gPwmData_prev = { _IQ(0.0), _IQ(0.0), _IQ(0.0) };

#ifdef DRV8301_SPI
// Watch window interface to the 8301 SPI
DRV_SPI_8301_Vars_t gDrvSpi8301Vars;
#endif

#ifdef DRV8305_SPI
// Watch window interface to the 8305 SPI
DRV_SPI_8305_Vars_t gDrvSpi8305Vars;
#endif

_iq gFlux_pu_to_Wb_sf;

_iq gFlux_pu_to_VpHz_sf;

_iq gTorque_Ls_Id_Iq_pu_to_Nm_sf;

_iq gTorque_Flux_Iq_pu_to_Nm_sf;

uint16_t boardId = 4;

volatile bool isVoltageTooLow = true;
_iq lowVoltageThreshold = _IQ(0.01);

char buf[16];
char returnBuf[32];
int counter = 0;
bool isCommandReceived = false;
bool isCommandStart = false;

bool isWaitingTxFifoEmpty = false;
int txOffDelayCount = 2; // 1 count = 66.667us, 15 counts = 1ms
int txOffDelayCounter = 0;
bool isTxOffDelayActive = false;
bool shouldSendSpeed = false;

bool isOpenLoop = false;

// define Angle Generate
ANGLE_GEN_Handle angle_genHandle;
ANGLE_GEN_Obj    angle_gen;

// define Vs per Freq
VS_FREQ_Handle vs_freqHandle;
VS_FREQ_Obj    vs_freq;

// **************************************************************************
// the functions

void main(void) {
	uint_least8_t estNumber = 0;

#ifdef FAST_ROM_V1p6
	uint_least8_t ctrlNumber = 0;
#endif

	// Only used if running from FLASH
	// Note that the variable FLASH is defined by the project
#ifdef FLASH
	// Copy time critical code and Flash setup code to RAM
	// The RamfuncsLoadStart, RamfuncsLoadEnd, and RamfuncsRunStart
	// symbols are created by the linker. Refer to the linker files.
	memCopy((uint16_t *) &RamfuncsLoadStart, (uint16_t *) &RamfuncsLoadEnd, (uint16_t *) &RamfuncsRunStart);

#ifdef CSM_ENABLE
	//copy .econst to unsecure RAM
	if(*econst_end - *econst_start) {
		memCopy((uint16_t *)&econst_start,(uint16_t *)&econst_end,(uint16_t *)&econst_ram_load);
	}

	//copy .switch ot unsecure RAM
	if(*switch_end - *switch_start) {
		memCopy((uint16_t *)&switch_start,(uint16_t *)&switch_end,(uint16_t *)&switch_ram_load);
	}
#endif
#endif

	// initialize the hardware abstraction layer
	halHandle = HAL_init(&hal, sizeof(hal));

	// check for errors in user parameters
	USER_checkForErrors(&gUserParams);

	// store user parameter error in global variable
	gMotorVars.UserErrorCode = USER_getErrorCode(&gUserParams);

	// do not allow code execution if there is a user parameter error
	if (gMotorVars.UserErrorCode != USER_ErrorCode_NoError) {
		for (;;) {
			gMotorVars.Flag_enableSys = false;
		}
	}

	// initialize the user parameters
	USER_setParams(&gUserParams);

	// set the hardware abstraction layer parameters
	HAL_setParams(halHandle, &gUserParams);

	// initialize the controller
#ifdef FAST_ROM_V1p6
	ctrlHandle = CTRL_initCtrl(ctrlNumber, estNumber); //v1p6 format (06xF and 06xM devices)
#else
	ctrlHandle = CTRL_initCtrl(estNumber, &ctrl, sizeof(ctrl));	//v1p7 format default
#endif

	controller_obj = (CTRL_Obj *)ctrlHandle;

	{
		CTRL_Version version;

		// get the version number
		CTRL_getVersion(ctrlHandle, &version);

		gMotorVars.CtrlVersion = version;
	}

	// set the default controller parameters
	CTRL_setParams(ctrlHandle, &gUserParams);

	// initialize the angle generate module
	angle_genHandle = ANGLE_GEN_init(&angle_gen,sizeof(angle_gen));
	ANGLE_GEN_setParams(angle_genHandle, gUserParams.iqFullScaleFreq_Hz, gUserParams.ctrlPeriod_sec);

	// initialize the Vs per Freq module
	vs_freqHandle = VS_FREQ_init(&vs_freq,sizeof(vs_freq));
	VS_FREQ_setParams(vs_freqHandle,  gUserParams.iqFullScaleFreq_Hz, gUserParams.iqFullScaleVoltage_V, gUserParams.maxVsMag_pu);
	VS_FREQ_setProfile(vs_freqHandle, USER_MOTOR_FREQ_LOW, USER_MOTOR_FREQ_HIGH, USER_MOTOR_VOLT_MIN, USER_MOTOR_VOLT_MAX);

	// Initialize and setup the 100% SVM generator
	svgencurrentHandle = SVGENCURRENT_init(&svgencurrent, sizeof(svgencurrent));

	// setup svgen current
	{
		float_t minWidth_microseconds = 2.0;
		uint16_t minWidth_counts = (uint16_t) (minWidth_microseconds * USER_SYSTEM_FREQ_MHz);
		float_t fdutyLimit = 0.5 - (2.0 * minWidth_microseconds * USER_PWM_FREQ_kHz * 0.001);
		_iq dutyLimit = _IQ(fdutyLimit);

		SVGENCURRENT_setMinWidth(svgencurrentHandle, minWidth_counts);
		SVGENCURRENT_setIgnoreShunt(svgencurrentHandle, use_all);
		SVGENCURRENT_setMode(svgencurrentHandle, all_phase_measurable);
		SVGENCURRENT_setVlimit(svgencurrentHandle, dutyLimit);
	}

	// set overmodulation to maximum value
	gMotorVars.OverModulation = _IQ(MATH_TWO_OVER_THREE);

	// setup faults
	HAL_setupFaults(halHandle);

	// initialize the interrupt vector table
	HAL_initIntVectorTable(halHandle);

	// enable the ADC interrupts
	HAL_enableAdcInts(halHandle);

	// enable the SCI interrupts
	HAL_enableSciInts(halHandle);

	// enable global interrupts
	HAL_enableGlobalInts(halHandle);

	// enable debug interrupts
	HAL_enableDebugInt(halHandle);

	// disable the PWM
	HAL_disablePwm(halHandle);

#ifdef DRV8301_SPI
	// turn on the DRV8301 if present
	HAL_enableDrv(halHandle);
	// initialize the DRV8301 interface
	HAL_setupDrvSpi(halHandle,&gDrvSpi8301Vars);
#endif

#ifdef DRV8305_SPI
	// turn on the DRV8305 if present
	HAL_enableDrv(halHandle);
	// initialize the DRV8305 interface
	HAL_setupDrvSpi(halHandle, &gDrvSpi8305Vars);
#endif

	// enable DC bus compensation
	CTRL_setFlag_enableDcBusComp(ctrlHandle, true);

	// compute scaling factors for flux and torque calculations
	gFlux_pu_to_Wb_sf = USER_computeFlux_pu_to_Wb_sf();
	gFlux_pu_to_VpHz_sf = USER_computeFlux_pu_to_VpHz_sf();
	gTorque_Ls_Id_Iq_pu_to_Nm_sf = USER_computeTorque_Ls_Id_Iq_pu_to_Nm_sf();
	gTorque_Flux_Iq_pu_to_Nm_sf = USER_computeTorque_Flux_Iq_pu_to_Nm_sf();

	gMotorVars.Kp_spd = _IQ(4.0);
	gMotorVars.MaxAccel_krpmps = _IQ(100.0);
	gMotorVars.SpeedRef_krpm = _IQ(0.0);
	gMotorVars.Flag_enableSys = true;
	gMotorVars.Flag_enableOffsetcalc = false;

	for (;;) {
		// Waiting for enable system flag to be set
		while (!(gMotorVars.Flag_enableSys));

		// Enable the Library internal PI.  Iq is referenced by the speed PI now
		CTRL_setFlag_enableSpeedCtrl(ctrlHandle, true);

		// loop while the enable system flag is true
		while (gMotorVars.Flag_enableSys) {
		    bool shouldRun = gMotorVars.Flag_Run_Identify;

			if (isCommandReceived) {
				isCommandReceived = false;

				if (buf[1] == boardId && buf[2] == 's') {
				    shouldRun = true;
				    gMotorVars.SpeedRef_krpm = ((long)buf[3]) | ((long)buf[4] << 8) | ((long)buf[5] << 16) | ((long)buf[6] << 24);
				}

				isCommandStart = false;
				counter = 0;
			}

			long currentSpeed = gMotorVars.Speed_krpm;
			long requestedSpeed = gMotorVars.SpeedRef_krpm;

			if (shouldSendSpeed) {
				shouldSendSpeed = false;

				if (buf[1] == boardId && buf[2] == 's') {
					returnBuf[0] = '<';
					returnBuf[1] = boardId;
					returnBuf[2] = 'd';

					returnBuf[3] = currentSpeed;
					returnBuf[4] = currentSpeed >> 8;
					returnBuf[5] = currentSpeed >> 16;
					returnBuf[6] = currentSpeed >> 24;
					returnBuf[7] = '>';

					serialWrite(returnBuf, 8);
				}
			}

			if (requestedSpeed == _IQ(0.0)) {
			    shouldRun = false;
                isOpenLoop = false;
            } else if (requestedSpeed <= _IQ(0.2) && currentSpeed <= _IQ(0.2)) {
                isOpenLoop = true;
            } else if (requestedSpeed > _IQ(0.2)) {
                isOpenLoop = false;
            }

			gMotorVars.Flag_Run_Identify = shouldRun;

			CTRL_Obj *obj = (CTRL_Obj *) ctrlHandle;

			// increment counters
			gCounter_updateGlobals++;

			// enable/disable the use of motor parameters being loaded from user.h
			CTRL_setFlag_enableUserMotorParams(ctrlHandle, gMotorVars.Flag_enableUserParams);

			// enable/disable Rs recalibration during motor startup
			EST_setFlag_enableRsRecalc(obj->estHandle, gMotorVars.Flag_enableRsRecalc);

			// enable/disable automatic calculation of bias values
			CTRL_setFlag_enableOffset(ctrlHandle, gMotorVars.Flag_enableOffsetcalc);

			if (CTRL_isError(ctrlHandle)) {
				// set the enable controller flag to false
				CTRL_setFlag_enableCtrl(ctrlHandle, false);

				// set the enable system flag to false
				gMotorVars.Flag_enableSys = false;

				// disable the PWM
				HAL_disablePwm(halHandle);
			} else if (isVoltageTooLow) {
				// set the enable controller flag to false
				CTRL_setFlag_enableCtrl(ctrlHandle, false);

				// disable the PWM
				HAL_disablePwm(halHandle);

				gMotorVars.Flag_Run_Identify = false;
			} else {
				// update the controller state
				bool flag_ctrlStateChanged = CTRL_updateState(ctrlHandle);

				// enable or disable the control
				CTRL_setFlag_enableCtrl(ctrlHandle, gMotorVars.Flag_Run_Identify);

				if (isOpenLoop) {
					// set flag to disable speed controller
					CTRL_setFlag_enableSpeedCtrl(ctrlHandle, false);

					// set flag to disable current controller
					CTRL_setFlag_enableCurrentCtrl(ctrlHandle, false);
				} else {
					// set flag to disable speed controller
					CTRL_setFlag_enableSpeedCtrl(ctrlHandle, true);

					// set flag to disable current controller
					CTRL_setFlag_enableCurrentCtrl(ctrlHandle, true);
				}

				if (flag_ctrlStateChanged) {
					CTRL_State_e ctrlState = CTRL_getState(ctrlHandle);

					if (ctrlState == CTRL_State_OffLine) {
						// enable the PWM
						HAL_enablePwm(halHandle);
					} else if (ctrlState == CTRL_State_OnLine) {
						if (gMotorVars.Flag_enableOffsetcalc == true) {
							// update the ADC bias values
							HAL_updateAdcBias(halHandle);
						} else {
							// set the current bias
							HAL_setBias(halHandle, HAL_SensorType_Current, 0, _IQ(I_A_offset));
							HAL_setBias(halHandle, HAL_SensorType_Current, 1, _IQ(I_B_offset));
							HAL_setBias(halHandle, HAL_SensorType_Current, 2, _IQ(I_C_offset));

							// set the voltage bias
							HAL_setBias(halHandle, HAL_SensorType_Voltage, 0, _IQ(V_A_offset));
							HAL_setBias(halHandle, HAL_SensorType_Voltage, 1, _IQ(V_B_offset));
							HAL_setBias(halHandle, HAL_SensorType_Voltage, 2, _IQ(V_C_offset));
						}

						// Return the bias value for currents
						gMotorVars.I_bias.value[0] = HAL_getBias(halHandle, HAL_SensorType_Current, 0);
						gMotorVars.I_bias.value[1] = HAL_getBias(halHandle, HAL_SensorType_Current, 1);
						gMotorVars.I_bias.value[2] = HAL_getBias(halHandle, HAL_SensorType_Current, 2);

						// Return the bias value for voltages
						gMotorVars.V_bias.value[0] = HAL_getBias(halHandle, HAL_SensorType_Voltage, 0);
						gMotorVars.V_bias.value[1] = HAL_getBias(halHandle, HAL_SensorType_Voltage, 1);
						gMotorVars.V_bias.value[2] = HAL_getBias(halHandle, HAL_SensorType_Voltage, 2);

						// enable the PWM
						HAL_enablePwm(halHandle);
					} else if (ctrlState == CTRL_State_Idle) {
						// disable the PWM
						HAL_disablePwm(halHandle);
						gMotorVars.Flag_Run_Identify = false;
					}

					if ((CTRL_getFlag_enableUserMotorParams(ctrlHandle) == true) && (ctrlState > CTRL_State_Idle)
							&& (gMotorVars.CtrlVersion.minor == 6)) {
						// call this function to fix 1p6
						USER_softwareUpdate1p6(ctrlHandle);
					}
				}
			}

			if (EST_isMotorIdentified(obj->estHandle)) {
				_iq Id_squared_pu = _IQmpy(CTRL_getId_ref_pu(ctrlHandle), CTRL_getId_ref_pu(ctrlHandle));

				//Set the maximum current controller output for the Iq and Id current controllers to enable
				//over-modulation.
				//An input into the SVM above 1/SQRT(3) = 0.5774 is in the over-modulation region.  An input of 0.5774 is where
				//the crest of the sinewave touches the 100% duty cycle.  At an input of 2/3, the SVM generator
				//produces a trapezoidal waveform touching every corner of the hexagon
				CTRL_setMaxVsMag_pu(ctrlHandle, gMotorVars.OverModulation);

				// set the current ramp
				EST_setMaxCurrentSlope_pu(obj->estHandle, gMaxCurrentSlope);
				gMotorVars.Flag_MotorIdentified = true;

				// set the speed reference
				CTRL_setSpd_ref_krpm(ctrlHandle, gMotorVars.SpeedRef_krpm);

				// set the speed acceleration
				CTRL_setMaxAccel_pu(ctrlHandle, _IQmpy(MAX_ACCEL_KRPMPS_SF, gMotorVars.MaxAccel_krpmps));

				// set the Id reference
				CTRL_setId_ref_pu(ctrlHandle, _IQmpy(gMotorVars.IdRef_A, _IQ(1.0/USER_IQ_FULL_SCALE_CURRENT_A)));

				if (Flag_Latch_softwareUpdate) {
					Flag_Latch_softwareUpdate = false;

					USER_calcPIgains(ctrlHandle);

					// initialize the watch window kp and ki current values with pre-calculated values
					gMotorVars.Kp_Idq = CTRL_getKp(ctrlHandle, CTRL_Type_PID_Id);
					gMotorVars.Ki_Idq = CTRL_getKi(ctrlHandle, CTRL_Type_PID_Id);
				}

			} else {
				Flag_Latch_softwareUpdate = true;

				// initialize the watch window kp and ki values with pre-calculated values
				//gMotorVars.Kp_spd = CTRL_getKp(ctrlHandle, CTRL_Type_PID_spd);
				gMotorVars.Ki_spd = CTRL_getKi(ctrlHandle, CTRL_Type_PID_spd);

				// the estimator sets the maximum current slope during identification
				gMaxCurrentSlope = EST_getMaxCurrentSlope_pu(obj->estHandle);
			}

			// when appropriate, update the global variables
			if (gCounter_updateGlobals >= NUM_MAIN_TICKS_FOR_GLOBAL_VARIABLE_UPDATE) {
				// reset the counter
				gCounter_updateGlobals = 0;

				updateGlobalVariables_motor(ctrlHandle);

				if (isVoltageTooLow && gMotorVars.VdcBus_kV > lowVoltageThreshold) {
					isVoltageTooLow = false;

					// Power restored, reset to start with fresh parameters
					// disable the PWM
					HAL_disablePwm(halHandle);

					// set the default controller parameters (Reset the control to re-identify the motor)
					CTRL_setParams(ctrlHandle, &gUserParams);
					gMotorVars.Flag_Run_Identify = false;
				} else if (!isVoltageTooLow && gMotorVars.VdcBus_kV < lowVoltageThreshold) {
					isVoltageTooLow = true;

					// Power lost, disable control
					if (gMotorVars.Flag_Run_Identify) {
						// disable the PWM
						HAL_disablePwm(halHandle);

						CTRL_setFlag_enableCtrl(ctrlHandle, false);

						// set the default controller parameters (Reset the control to re-identify the motor)
						CTRL_setParams(ctrlHandle, &gUserParams);
						gMotorVars.Flag_Run_Identify = false;
					}
				}
			}

			// update Kp and Ki gains
			updateKpKiGains(ctrlHandle);

			// enable/disable the forced angle
			EST_setFlag_enableForceAngle(obj->estHandle, gMotorVars.Flag_enableForceAngle);

			// enable or disable power warp
			CTRL_setFlag_enablePowerWarp(ctrlHandle, gMotorVars.Flag_enablePowerWarp);

#ifdef DRV8301_SPI
			HAL_writeDrvData(halHandle,&gDrvSpi8301Vars);

			HAL_readDrvData(halHandle,&gDrvSpi8301Vars);
#endif
#ifdef DRV8305_SPI
			HAL_writeDrvData(halHandle, &gDrvSpi8305Vars);

			HAL_readDrvData(halHandle, &gDrvSpi8305Vars);
#endif
		} // end of while(gFlag_enableSys) loop

		// disable the PWM
		HAL_disablePwm(halHandle);

		// set the default controller parameters (Reset the control to re-identify the motor)
		CTRL_setParams(ctrlHandle, &gUserParams);
		gMotorVars.Flag_Run_Identify = false;

	} // end of for(;;) loop

} // end of main() function

interrupt void mainISR(void) {
	SVGENCURRENT_MeasureShunt_e measurableShuntThisCycle = SVGENCURRENT_getMode(svgencurrentHandle);

	// acknowledge the ADC interrupt
	HAL_acqAdcInt(halHandle, ADC_IntNumber_1);

	// convert the ADC data
	HAL_readAdcData(halHandle, &gAdcData);

	if (!isOpenLoop) {
		// run the current reconstruction algorithm
		SVGENCURRENT_RunRegenCurrent(svgencurrentHandle, (MATH_vec3 *) (gAdcData.I.value));

		gIavg.value[0] += (gAdcData.I.value[0] - gIavg.value[0]) >> gIavg_shift;
		gIavg.value[1] += (gAdcData.I.value[1] - gIavg.value[1]) >> gIavg_shift;
		gIavg.value[2] += (gAdcData.I.value[2] - gIavg.value[2]) >> gIavg_shift;

		if (measurableShuntThisCycle > two_phase_measurable) {
			gAdcData.I.value[0] = gIavg.value[0];
			gAdcData.I.value[1] = gIavg.value[1];
			gAdcData.I.value[2] = gIavg.value[2];
		}
	}

	uint_least16_t count_isr = CTRL_getCount_isr(ctrlHandle);
	uint_least16_t numIsrTicksPerCtrlTick = CTRL_getNumIsrTicksPerCtrlTick(ctrlHandle);

	if (isOpenLoop) {
		// if needed, run the controller
		if (count_isr >= numIsrTicksPerCtrlTick) {
			CTRL_State_e ctrlState = CTRL_getState(ctrlHandle);

			// reset the isr count
			CTRL_resetCounter_isr(ctrlHandle);

			// increment the state counter
			CTRL_incrCounter_state(ctrlHandle);

			// increment the trajectory count
			CTRL_incrCounter_traj(ctrlHandle);

			// run the appropriate controller
			if (ctrlState == CTRL_State_OnLine) {
				// increment the current count
				CTRL_incrCounter_current(ctrlHandle);

				// increment the speed count
				CTRL_incrCounter_speed(ctrlHandle);

				MATH_vec2 phasor;

				// run Clarke transform on current
				CLARKE_run(controller_obj->clarkeHandle_I, &gAdcData.I, CTRL_getIab_in_addr(ctrlHandle));

				// run Clarke transform on voltage
				CLARKE_run(controller_obj->clarkeHandle_V, &gAdcData.V, CTRL_getVab_in_addr(ctrlHandle));

				controller_obj->speed_ref_pu = TRAJ_getIntValue(controller_obj->trajHandle_spd);

				ANGLE_GEN_run(angle_genHandle, controller_obj->speed_ref_pu);
				VS_FREQ_run(vs_freqHandle, controller_obj->speed_ref_pu);

				// generate the motor electrical angle
				controller_obj->angle_pu = ANGLE_GEN_getAngle_pu(angle_genHandle);

				controller_obj->Vdq_out.value[0] = vs_freq.Vdq_out.value[0];
				controller_obj->Vdq_out.value[1] = vs_freq.Vdq_out.value[1];

				// compute the sin/cos phasor
				CTRL_computePhasor(controller_obj->angle_pu, &phasor);

				// set the phasor in the Park transform
				PARK_setPhasor(controller_obj->parkHandle, &phasor);

				// run the Park transform
				PARK_run(controller_obj->parkHandle, CTRL_getIab_in_addr(ctrlHandle), CTRL_getIdq_in_addr(ctrlHandle));

				// set the phasor in the inverse Park transform
				IPARK_setPhasor(controller_obj->iparkHandle, &phasor);

				// run the inverse Park module
				IPARK_run(controller_obj->iparkHandle, CTRL_getVdq_out_addr(ctrlHandle),
						CTRL_getVab_out_addr(ctrlHandle));

				// run the space Vector Generator (SVGEN) module
				SVGEN_run(controller_obj->svgenHandle, CTRL_getVab_out_addr(ctrlHandle), &(gPwmData.Tabc));
			} else if (ctrlState == CTRL_State_OffLine) {
				// run the offline controller
				CTRL_runOffLine(ctrlHandle, halHandle, &gAdcData, &gPwmData);
			} else if (ctrlState == CTRL_State_Idle) {
				// set all pwm outputs to zero
				gPwmData.Tabc.value[0] = _IQ(0.0);
				gPwmData.Tabc.value[1] = _IQ(0.0);
				gPwmData.Tabc.value[2] = _IQ(0.0);
			}
		} else {
			// increment the isr count
			CTRL_incrCounter_isr(ctrlHandle);
		}

		CTRL_setup(ctrlHandle);

		// write the PWM compare values
		HAL_writePwmData(halHandle,&gPwmData);
	} else {

		// if needed, run the controller
		if (count_isr >= numIsrTicksPerCtrlTick) {
			CTRL_State_e ctrlState = CTRL_getState(ctrlHandle);

			// reset the isr count
			CTRL_resetCounter_isr(ctrlHandle);

			// increment the state counter
			CTRL_incrCounter_state(ctrlHandle);

			// increment the trajectory count
			CTRL_incrCounter_traj(ctrlHandle);

			// run the appropriate controller
			if (ctrlState == CTRL_State_OnLine) {

				// increment the current count
				CTRL_incrCounter_current(ctrlHandle);

				// increment the speed count
				CTRL_incrCounter_speed(ctrlHandle);

				CTRL_runOnLine_User(ctrlHandle, &gAdcData, &gPwmData);

			} else if (ctrlState == CTRL_State_OffLine) {
				// run the offline controller
				CTRL_runOffLine(ctrlHandle, halHandle, &gAdcData, &gPwmData);
			} else if (ctrlState == CTRL_State_Idle) {
				// set all pwm outputs to zero
				gPwmData.Tabc.value[0] = _IQ(0.0);
				gPwmData.Tabc.value[1] = _IQ(0.0);
				gPwmData.Tabc.value[2] = _IQ(0.0);
			}
		} else {
			// increment the isr count
			CTRL_incrCounter_isr(ctrlHandle);
		}

		// run the PWM compensation and current ignore algorithm
		SVGENCURRENT_compPwmData(svgencurrentHandle, &(gPwmData.Tabc), &gPwmData_prev);

		// write the PWM compare values
		HAL_writePwmData(halHandle, &gPwmData);

		{
			SVGENCURRENT_IgnoreShunt_e ignoreShuntNextCycle = SVGENCURRENT_getIgnoreShunt(svgencurrentHandle);
			SVGENCURRENT_VmidShunt_e midVolShunt = SVGENCURRENT_getVmid(svgencurrentHandle);

			// Set trigger point in the middle of the low side pulse
			HAL_setTrigger(halHandle, ignoreShuntNextCycle, midVolShunt);
		}

		// setup the controller
		CTRL_setup(ctrlHandle);
	}

	if (isTxOffDelayActive) {
		if (++txOffDelayCounter == txOffDelayCount) {
			txOffDelayCounter = 0;
			isTxOffDelayActive = false;
			GPIO_setLow(halHandle->gpioHandle, GPIO_Number_12);
		}
	}

	if (isWaitingTxFifoEmpty && SCI_getRxFifoStatus(halHandle->sciAHandle) == SCI_FifoStatus_Empty) {
		isWaitingTxFifoEmpty = 0;
		isTxOffDelayActive = true;
	}

	return;
} // end of mainISR() function

interrupt void sciARxISR(void) {
	HAL_Obj *obj = (HAL_Obj *)halHandle;

	while (SCI_rxDataReady(obj->sciAHandle)) {
		char c = SCI_read(obj->sciAHandle);

		if (counter < 8) {
			switch (counter) {
			case 0:
				if (c == '<') {
					buf[counter] = c;
					counter++;
				} else {
					counter = 0;
				}
				break;
			case 1:
				if (c == boardId) {
					buf[counter] = c;
					counter++;
				} else {
					counter = 0;
				}
				break;
			case 2:
				if (c == 's') {
					buf[counter] = c;
					counter++;
					shouldSendSpeed = true;
				} else {
					counter = 0;
				}
				break;
			case 3:
			case 4:
			case 5:
			case 6:
				buf[counter] = c;
				counter++;
				break;
			case 7:
				if (c == '>') {
					buf[counter] = c;
					isCommandReceived = true;
				} else {
					counter = 0;
				}
				break;
			default:
				counter = 0;
			}
		}
	}

	// acknowledge interrupt from SCI group so that SCI interrupt is not received twice
	PIE_clearInt(obj->pieHandle, PIE_GroupNumber_9);
}

void serialWrite(char *sendData, int length) {
	int i = 0;

	GPIO_setHigh(halHandle->gpioHandle, GPIO_Number_12);

	while (i < length) {
		if (SCI_txReady(halHandle->sciAHandle)) {
			SCI_write(halHandle->sciAHandle, sendData[i]);
			i++;
		}
	}

	isWaitingTxFifoEmpty = true;
}

void updateGlobalVariables_motor(CTRL_Handle handle) {
	CTRL_Obj *obj = (CTRL_Obj *) handle;

	// get the speed estimate
	gMotorVars.Speed_krpm = EST_getSpeed_krpm(obj->estHandle);

	// get the real time speed reference coming out of the speed trajectory generator
	gMotorVars.SpeedTraj_krpm = _IQmpy(CTRL_getSpd_int_ref_pu(handle), EST_get_pu_to_krpm_sf(obj->estHandle));

	// get the torque estimate
	gMotorVars.Torque_Nm = USER_computeTorque_Nm(handle, gTorque_Flux_Iq_pu_to_Nm_sf, gTorque_Ls_Id_Iq_pu_to_Nm_sf);

	// get the magnetizing current
	gMotorVars.MagnCurr_A = EST_getIdRated(obj->estHandle);

	// get the rotor resistance
	gMotorVars.Rr_Ohm = EST_getRr_Ohm(obj->estHandle);

	// get the stator resistance
	gMotorVars.Rs_Ohm = EST_getRs_Ohm(obj->estHandle);

	// get the stator inductance in the direct coordinate direction
	gMotorVars.Lsd_H = EST_getLs_d_H(obj->estHandle);

	// get the stator inductance in the quadrature coordinate direction
	gMotorVars.Lsq_H = EST_getLs_q_H(obj->estHandle);

	// get the flux in V/Hz in floating point
	gMotorVars.Flux_VpHz = EST_getFlux_VpHz(obj->estHandle);

	// get the flux in Wb in fixed point
	gMotorVars.Flux_Wb = USER_computeFlux(handle, gFlux_pu_to_Wb_sf);

	// get the controller state
	gMotorVars.CtrlState = CTRL_getState(handle);

	// get the estimator state
	gMotorVars.EstState = EST_getState(obj->estHandle);

	// read Vd and Vq vectors per units
	gMotorVars.Vd = CTRL_getVd_out_pu(ctrlHandle);
	gMotorVars.Vq = CTRL_getVq_out_pu(ctrlHandle);

	// calculate vector Vs in per units
	gMotorVars.Vs = _IQsqrt(_IQmpy(gMotorVars.Vd, gMotorVars.Vd) + _IQmpy(gMotorVars.Vq, gMotorVars.Vq));

	// read Id and Iq vectors in amps
	gMotorVars.Id_A = _IQmpy(CTRL_getId_in_pu(ctrlHandle), _IQ(USER_IQ_FULL_SCALE_CURRENT_A));
	gMotorVars.Iq_A = _IQmpy(CTRL_getIq_in_pu(ctrlHandle), _IQ(USER_IQ_FULL_SCALE_CURRENT_A));

	// calculate vector Is in amps
	gMotorVars.Is_A = _IQsqrt(_IQmpy(gMotorVars.Id_A, gMotorVars.Id_A) + _IQmpy(gMotorVars.Iq_A, gMotorVars.Iq_A));

	// Get the DC buss voltage
	gMotorVars.VdcBus_kV = _IQmpy(gAdcData.dcBus, _IQ(USER_IQ_FULL_SCALE_VOLTAGE_V/1000.0));

	return;
} // end of updateGlobalVariables_motor() function

void updateKpKiGains(CTRL_Handle handle) {
	if ((gMotorVars.CtrlState == CTRL_State_OnLine) && (gMotorVars.Flag_MotorIdentified == true)
			&& (Flag_Latch_softwareUpdate == false)) {
		// set the kp and ki speed values from the watch window
		CTRL_setKp(handle, CTRL_Type_PID_spd, gMotorVars.Kp_spd);
		CTRL_setKi(handle, CTRL_Type_PID_spd, gMotorVars.Ki_spd);

		// set the kp and ki current values for Id and Iq from the watch window
		CTRL_setKp(handle, CTRL_Type_PID_Id, gMotorVars.Kp_Idq);
		CTRL_setKi(handle, CTRL_Type_PID_Id, gMotorVars.Ki_Idq);
		CTRL_setKp(handle, CTRL_Type_PID_Iq, gMotorVars.Kp_Idq);
		CTRL_setKi(handle, CTRL_Type_PID_Iq, gMotorVars.Ki_Idq);
	}

	return;
} // end of updateKpKiGains() function

//@} //defgroup
// end of file

