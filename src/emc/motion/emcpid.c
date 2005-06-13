// NOTE: emcpid.c is no longer used by emc2, the PID function is
// supplied by the HAL when needed.  Stepper based systems don't
// use PID at all.  This file will eventually be deleted, but is
// being kept for reference for now.
/********************************************************************
* Description: emcpid.c
*   C definitions for PID code
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
* Last change:
* $Revision$
* $Author$
* $Date$
********************************************************************/

#ifdef ULAPI
#include <stdio.h>
#include "inifile.h"
#endif

#include "rtapi.h"		/* rtapi_print_msg */
#include "posemath.h"
#include "emcpid.h"		/* these decls */

#define GAINS_SET 0x01
#define CYCLE_TIME_SET 0x02
#define ALL_SET (GAINS_SET | CYCLE_TIME_SET)

int pidInit(PID_STRUCT * pid)
{
#ifdef SMOOTH_D_FACTOR
    int i;
#endif
    if (0 == pid) {
	return -1;
    }

    pid->p = 0.0;
    pid->i = 0.0;
    pid->d = 0.0;
    pid->ff0 = 0.0;
    pid->ff1 = 0.0;
    pid->ff2 = 0.0;
    pid->backlash = 0.0;
    pid->bias = 0.0;
    pid->maxError = 0.0;
    pid->deadband = 0.0;
    pid->cycleTime = 1.0;
    pid->configured = 0;

#ifdef SMOOTH_D_FACTOR
    for (i = 0; i < MAX_ERROR_WINDOW; i++) {
	pid->oldErrors[i] = 0.0;
	pid->errorWindowFactors[i] = 0.1;
    }
    pid->errorIndex = 0;
#endif

    /* now clear out the state vars */
    return pidReset(pid);
}

/* clear out the state vars */
int pidReset(PID_STRUCT * pid)
{
    if (0 == pid) {
	return -1;
    }

    pid->error = 0.0;
    pid->lastError = 0.0;
    pid->cumError = 0.0;
    pid->lastSetpoint = 0.0;
    pid->lastSetpoint_was_set = 0;	/* lastSetpoint is no longer valid. */
    pid->lastSetpointDot = 0.0;
    pid->lastOutput = 0.0;

    return 0;
}

int pidSetCycleTime(PID_STRUCT * pid, double seconds)
{
    if (0 == pid || seconds <= 0.0) {
	return -1;
    }

    pid->cycleTime = seconds;
    pid->configured |= CYCLE_TIME_SET;

    return 0;
}

int pidSetGains(PID_STRUCT * pid, PID_STRUCT parameters)
{
#ifdef SMOOTH_D_FACTOR
    int i;
#endif

    if (0 == pid) {
	return -1;
    }

    pid->p = parameters.p;
    pid->i = parameters.i;
    pid->d = parameters.d;
    pid->ff0 = parameters.ff0;
    pid->ff1 = parameters.ff1;
    pid->ff2 = parameters.ff2;
    pid->backlash = parameters.backlash;
    pid->bias = parameters.bias;
    pid->maxError = parameters.maxError;
    pid->deadband = parameters.deadband;

#ifdef SMOOTH_D_FACTOR
    for (i = 0; i < MAX_ERROR_WINDOW; i++) {
	pid->oldErrors[i] = 0.0;
/*! \todo Another #if 0 */
#if 0
	if (parameters.errorWindowFactors[0] > 0.0) {
	    pid->errorWindowFactors[i] = parameters.errorWindowFactors[i];
	}
#endif
    }
    pid->errorIndex = 0;
#endif

    pid->configured |= GAINS_SET;

    return 0;
}

double pidRunCycle(PID_STRUCT * pid, double sample, double setpoint)
{
    double setpointDot;
    double error;
#ifdef SMOOTH_D_FACTOR
    double smooth_d;
    int i;
#endif

    if (0 == pid) {
	return 0;
    }

    if (pid->cycleTime < 1e-7 || pid->configured != ALL_SET) {
	return 0.0;
    }
    if (!pid->lastSetpoint_was_set) {
	pid->lastSetpoint = setpoint;
	pid->lastSetpointDot = 0.0;
	pid->lastSetpoint_was_set = 1;
    }

    /* calculate error and cumError */
    error = setpoint - sample;
    if (error > pid->deadband) {
	pid->error = error - pid->deadband;
    } else if (error < -pid->deadband) {
	pid->error = error + pid->deadband;
    } else {
	pid->error = 0.0;
    }
    pid->cumError += pid->error * pid->cycleTime;
    setpointDot = (setpoint - pid->lastSetpoint) / pid->cycleTime;
    if (pid->cumError > pid->maxError) {
	pid->cumError = pid->maxError;
    } else if (pid->cumError < -pid->maxError) {
	pid->cumError = -pid->maxError;
    }
#ifdef THROWAWAY_CUMULATIVE_ERROR_ON_SIGN_CHANGES
    if ((pid->error >= 0.0 && pid->lastError <= 0.0) ||
	(pid->error <= 0.0 && pid->lastError >= 0.0)) {
	pid->cumError = pid->error * pid->cycleTime;
    }
#endif

#ifdef SMOOTH_D_FACTOR
    smooth_d =
	(pid->oldErrors[(pid->errorIndex +
		2 * MAX_ERROR_WINDOW) % MAX_ERROR_WINDOW]
	- pid->oldErrors[(pid->errorIndex + 1 +
		2 * MAX_ERROR_WINDOW) % MAX_ERROR_WINDOW]) / MAX_ERROR_WINDOW;
#ifdef ULAPI
    if (error != 0.0) {
	for (i = 0; i < MAX_ERROR_WINDOW; i++) {
	    printf("%8.8f ",
		(pid->oldErrors[(pid->errorIndex - i +
			    2 * MAX_ERROR_WINDOW) % MAX_ERROR_WINDOW]
		    - pid->oldErrors[(pid->errorIndex - i - 1 +
			    2 * MAX_ERROR_WINDOW) % MAX_ERROR_WINDOW]));
	}
    }
#endif
    pid->errorIndex++;
    pid->errorIndex %= MAX_ERROR_WINDOW;
    pid->oldErrors[pid->errorIndex] = pid->error;
#ifdef ULAPI
    if (error != 0.0) {
	printf("\nerror=%8.8f,(error-lastError)=%8.8f,smooth_d=%8.8f\n",
	    pid->error, (pid->error - pid->lastError), smooth_d);
    }
#endif
#endif

    /* do compensation calculations */
    pid->lastOutput = pid->bias +
	pid->p * pid->error + pid->i * pid->cumError +
#ifdef SMOOTH_D_FACTOR
	pid->d * smooth_d / pid->cycleTime +
#else
	pid->d * (pid->error - pid->lastError) / pid->cycleTime +
#endif
	pid->ff0 * setpoint +
	pid->ff1 * setpointDot +
	pid->ff2 * (setpointDot - pid->lastSetpointDot) / pid->cycleTime;

    /* update history vars */
    pid->lastSetpoint = setpoint;
    pid->lastSetpointDot = setpointDot;
    pid->lastError = pid->error;

    return pid->lastOutput;
}

int pidIniLoad(PID_STRUCT * pid, const char *filename)
{
#ifdef ULAPI

    PID_STRUCT params;
    double cycleTime;
    int retval = 0;
    const char *inistring;

    FILE *fp;

    if (NULL == (fp = fopen(filename, "r"))) {
	rtapi_print_msg(1, "can't open ini file %s\n", filename);
	return -1;
    }

    /* forward gains */

    if (NULL != (inistring = iniFind(fp, "P", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.p)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid P gain: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "I", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.i)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid I gain: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "D", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.d)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid D gain: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "FF0", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.ff0)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid FF0 gain: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "FF1", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.ff1)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid FF1 gain: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "FF2", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.ff2)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid FF2 gain: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "BACKLASH", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.backlash)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid backlash: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "BIAS", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.bias)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid bias: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "MAX_ERROR", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.maxError)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid max cum error: %s\n", inistring);
	    retval = -1;
	}
    }

    if (NULL != (inistring = iniFind(fp, "DEADBAND", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &params.deadband)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid deadband: %s\n", inistring);
	    retval = -1;
	}
    }

    /* if they all read ok, stick them in pid */
    if (retval == 0) {
	pidSetGains(pid, params);
    }

    if (NULL != (inistring = iniFind(fp, "CYCLE_TIME", 0))) {
	if (1 != sscanf((char *) inistring, "%lf", &cycleTime)) {
	    /* found, but invalid */
	    rtapi_print_msg(1, "invalid CYCLE_TIME: %s\n", inistring);
	    retval = -1;
	} else {
	    pidSetCycleTime(pid, cycleTime);
	}
    }

    /* close inifile */
    fclose(fp);

    return retval;

#else

    return -1;

#endif
}
