/* ---------------------------------------------------------------------------*
 * Sample implementation of an FMU - water tank. 
 * Equations:
 *  der(level) = rate;
 *  when level>H then pump := false;
 *  when level<L then pump := true;
 *  when pump == true then rate := v1 - v2 else rate := -v2;
 *  where
 *    H         water level upper threshold, parameter, start = 14
 *    L         water level lower threshold, parameter, start = 1
 *    v1        input flow water rate (from the puml to the tank), parameter, start = 3
 *    v2        output flow water rate (tank leakage), parameter, start = 2
 *    level     water level, state variable, start = 1
 *    der_level water flow rate
 *    pump      physical pump state, state variable, start = true
 *    
 * (c) University of Southampton
 * ---------------------------------------------------------------------------*/

// define class name and unique id
#define MODEL_IDENTIFIER waterTankCtr
#define MODEL_GUID "{cc4e810f-3df3-4a00-8276-176fa3c9f003}"

// define model size
#define NUMBER_OF_REALS 3
#define NUMBER_OF_INTEGERS 0
#define NUMBER_OF_BOOLEANS 1
#define NUMBER_OF_STRINGS 0
#define NUMBER_OF_STATES 0
#define NUMBER_OF_EVENT_INDICATORS 2

// include fmu header files, typedefs and macros
#include "fmuTemplate.h"

// define all model variables and their value references
// conventions used here:
// - if x is a variable, then macro x_ is its variable reference
// - the vr of a variable is its index in array  r, i, b or s
// - if k is the vr of a real state, then k+1 is the vr of its derivative

#define H_          0
#define L_          1
#define level_      2
#define pump_       0

// index of events
#define level_max_      0
#define level_min_      1
static fmiReal prevLevel;


// define initial state vector as vector of value references
#define STATES { }

// Linux: Functions in this file are declared to be static so
// that when the fmu_* method invokes one of these methods, then
// it gets the definition in the same shared library instead
// of getting the method with the same name in a previously
// loaded shared library.

// called by fmiInstantiateModel
// Set values for all variables that define a start value
// Settings used unless changed by fmiSetX before fmiInitialize
//TODO: read actual values from the modelDescription.xml
static void setStartValues(ModelInstance *comp) {
    comp->r[H_]         = 14;
    comp->r[L_]         = 1;
    comp->r[level_]     = 1;
    comp->b[pump_]      = fmiTrue;
    prevLevel = comp->r[level_];

    // events
    comp->isPositive[level_max_] = comp->r[level_] > comp->r[H_];
    comp->isPositive[level_min_] = comp->r[level_] < comp->r[L_];
}

// called by fmiGetReal, fmiGetContinuousStates and fmiGetDerivatives
static fmiReal getReal(ModelInstance* comp, fmiValueReference vr){
    switch (vr) {
        case H_         : return comp->r[H_];
        case L_         : return comp->r[L_];
        case level_     : return comp->r[level_];
        default: return 0;
    }
}

// called by fmiGetBoolean
static fmiBoolean getBoolean(ModelInstance* comp, fmiValueReference vr){
    switch (vr) {
        case pump_  : return comp->b[pump_];
        default: return fmiFalse;
    }
}

// called by fmiInitialize() after setting eventInfo to defaults
// Used to set the first time event, if any.
static void initialize(ModelInstance* comp, fmiEventInfo* eventInfo) {
}

// offset for event indicator, adds hysteresis and prevents z=0 at restart 
#define EPS_INDICATORS 1e-14

static fmiReal getEventIndicator(ModelInstance* comp, int z) {
    fmiReal level;
    switch (z) {
        case level_min_ :
            level = prevLevel - comp->r[L_];
            prevLevel = comp->r[level_];
            return level + (!comp->isPositive[level_min_] ? EPS_INDICATORS : -EPS_INDICATORS);
        case level_max_ :
            level = comp->r[H_] - prevLevel;
            prevLevel = comp->r[level_];
            return level + (!comp->isPositive[level_max_] ? EPS_INDICATORS : -EPS_INDICATORS);
        default: return 0;
    }
}

// Used to set the next time event, if any.
static void eventUpdate(ModelInstance* comp, fmiEventInfo* eventInfo) {
    comp->isPositive[level_max_] = comp->r[level_] > comp->r[H_];
    comp->isPositive[level_min_] = comp->r[level_] < comp->r[L_];
    
    // if level > H then pump = true
    if (comp->isPositive[level_max_]) {
        comp->b[pump_] = fmiFalse;
    }
    // if level < L then pump = false
    if (comp->isPositive[level_min_]) {
        comp->b[pump_] = fmiTrue;
    }

    eventInfo->iterationConverged  = fmiTrue;
    eventInfo->stateValueReferencesChanged = fmiFalse;
    eventInfo->stateValuesChanged  = fmiTrue;
    eventInfo->terminateSimulation = fmiFalse;
    eventInfo->upcomingTimeEvent   = fmiFalse;
 } 

// include code that implements the FMI based on the above definitions
#include "fmuTemplate.c"


