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
#define MODEL_IDENTIFIER waterTankEnv
#define MODEL_GUID "{ec4e810f-3df3-4a00-8276-176fa3c9f003}"

// define model size
#define NUMBER_OF_REALS 4
#define NUMBER_OF_INTEGERS 0
#define NUMBER_OF_BOOLEANS 1
#define NUMBER_OF_STRINGS 0
#define NUMBER_OF_STATES 1 //XXX             
#define NUMBER_OF_EVENT_INDICATORS 1

// include fmu header files, typedefs and macros
#include "fmuTemplate.h"

// define all model variables and their value references
// conventions used here:
// - if x is a variable, then macro x_ is its variable reference
// - the vr of a variable is its index in array  r, i, b or s
// - if k is the vr of a real state, then k+1 is the vr of its derivative

#define v1_         0
#define v2_         1
#define level_      2
#define der_level_  3
#define pump_       0

// index of events
#define pump_switch_    0
static fmiBoolean prevPump;

// define initial state vector as vector of value references
#define STATES { level_ }

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
    comp->r[v1_]        = 3;
    comp->r[v2_]        = 2;
    comp->r[level_]     = 1;
    comp->r[der_level_] = comp->r[v1_] - comp->r[v2_];
    comp->b[pump_]      = fmiTrue;
    prevPump = comp->b[pump_];

    // events
    comp->isPositive[pump_switch_] = prevPump != comp->b[pump_];
}

// called by fmiGetReal, fmiGetContinuousStates and fmiGetDerivatives
static fmiReal getReal(ModelInstance* comp, fmiValueReference vr){
    switch (vr) {
        case v1_        : return comp->r[v1_];
        case v2_        : return comp->r[v2_];
        case level_     : return comp->r[level_];
        case der_level_ : return comp->r[der_level_];
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
    fmiReal result;
    switch (z) {
        case pump_switch_ :
            // update event flag
            comp->isPositive[pump_switch_] = prevPump != comp->b[pump_];
            // update previous pump value
            prevPump = comp->b[pump_];
            return !comp->isPositive[pump_switch_] ? EPS_INDICATORS : -EPS_INDICATORS;
        default: return 0;
    }
}

// Used to set the next time event, if any.
static void eventUpdate(ModelInstance* comp, fmiEventInfo* eventInfo) {
    // der(level) = if pump then v1 - v2 else -v2;
    comp->r[der_level_] = comp->b[pump_] ? comp->r[v1_] - comp->r[v2_] : - comp->r[v2_];

    eventInfo->iterationConverged  = fmiTrue;
    eventInfo->stateValueReferencesChanged = fmiFalse;
    eventInfo->stateValuesChanged  = fmiTrue;
    eventInfo->terminateSimulation = fmiFalse;
    eventInfo->upcomingTimeEvent   = fmiFalse;
 } 

// include code that implements the FMI based on the above definitions
#include "fmuTemplate.c"


