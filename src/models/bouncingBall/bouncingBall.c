/* ---------------------------------------------------------------------------*
 * Sample implementation of an FMU - a bouncing ball. 
 * This demonstrates the use of state events and reinit of states.
 * Equations:
 *  der(h) = v;
 *  der(v) = -g;
 *  when h<0 then v := -e * v;  
 *  where
 *    h      height [m], used as state, start = 1
 *    v      velocity of ball [m/s], used as state
 *    der(h) velocity of ball [m/s] 
 *    der(v) acceleration of ball [m/s2] 
 *    g      acceleration of gravity [m/s2], a parameter, start = 9.81 
 *    e      a dimensionless parameter, start = 0.7
 *    
 * (c) 2010 QTronic GmbH 
 * ---------------------------------------------------------------------------*/

// define class name and unique id
#define MODEL_IDENTIFIER bouncingBall
#define MODEL_GUID "{8c4e810f-3df3-4a00-8276-176fa3c9f003}"

// define model size
#define NUMBER_OF_REALS 5
#define NUMBER_OF_INTEGERS 0
#define NUMBER_OF_BOOLEANS 0
#define NUMBER_OF_STRINGS 0
#define NUMBER_OF_STATES 2
#define NUMBER_OF_EVENT_INDICATORS 1

// include fmu header files, typedefs and macros
#include "fmuTemplate.h"

// define all model variables and their value references
// conventions used here:
// - if x is a variable, then macro x_ is its variable reference
// - the vr of a variable is its index in array  r, i, b or s
// - if k is the vr of a real state, then k+1 is the vr of its derivative
#define h_      0
#define der_h_  1
#define v_      2
#define der_v_  3
#define g_      3 // negated alias
#define e_      4

// define initial state vector as vector of value references
#define STATES { h_, v_ }

// Linux: Functions in this file are declared to be static so
// that when the fmu_* method invokes one of these methods, then
// it gets the definition in the same shared library instead
// of getting the method with the same name in a previously
// loaded shared library.

// called by fmiInstantiateModel
// Set values for all variables that define a start value
// Settings used unless changed by fmiSetX before fmiInitialize
static void setStartValues(ModelInstance *comp) {
    comp->r[h_]     =  1;
    comp->r[v_]     =  0;
    comp->r[der_v_] = -9.81;
    comp->r[e_]     =  0.7;
    comp->isPositive[0] = comp->r[h_] > 0;
}

// called by fmiGetReal, fmiGetContinuousStates and fmiGetDerivatives
static fmiReal getReal(ModelInstance* comp, fmiValueReference vr){
    switch (vr) {
        case h_     : return comp->r[h_];
        case der_h_ : return comp->r[v_];
        case v_     : return comp->r[v_];
        case der_v_ : return comp->r[der_v_];
        case e_     : return comp->r[e_];
        default: return 0;
    }
}

// called by fmiInitialize() after setting eventInfo to defaults
// Used to set the first time event, if any.
static void initialize(ModelInstance* comp, fmiEventInfo* eventInfo) {
}

// offset for event indicator, adds hysteresis and prevents z=0 at restart 
#define EPS_INDICATORS 1e-14

static fmiReal getEventIndicator(ModelInstance* comp, int z) {
    switch (z) {
        case 0 : return comp->r[h_] + (comp->isPositive[0] ? EPS_INDICATORS : -EPS_INDICATORS);
        default: return 0;
    }
}

// Used to set the next time event, if any.
static void eventUpdate(ModelInstance* comp, fmiEventInfo* eventInfo) {
    if (comp->isPositive[0]) {
        comp->r[v_] = - comp->r[e_] * comp->r[v_];
    }
    comp->isPositive[0] = comp->r[h_] > 0;
    eventInfo->iterationConverged  = fmiTrue;
    eventInfo->stateValueReferencesChanged = fmiFalse;
    eventInfo->stateValuesChanged  = fmiTrue;
    eventInfo->terminateSimulation = fmiFalse;
    eventInfo->upcomingTimeEvent   = fmiFalse;
 } 

// include code that implements the FMI based on the above definitions
#include "fmuTemplate.c"


