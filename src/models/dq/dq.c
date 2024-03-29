/* ---------------------------------------------------------------------------*
 * Sample implementation of an FMU - the Dahlquist test equation. 
 *
 *   der(x) = - k * x and x(0) = 1. 
 *   Analytical solution: x(t) = exp(-k*t).
 *
 * (c) 2010 QTronic GmbH 
 * ---------------------------------------------------------------------------*/

// define class name and unique id
#define MODEL_IDENTIFIER dq
#define MODEL_GUID "{8c4e810f-3df3-4a00-8276-176fa3c9f000}"

// define model size
#define NUMBER_OF_REALS 3
#define NUMBER_OF_INTEGERS 0
#define NUMBER_OF_BOOLEANS 0
#define NUMBER_OF_STRINGS 0
#define NUMBER_OF_STATES 1
#define NUMBER_OF_EVENT_INDICATORS 0

// include fmu header files, typedefs and macros
#include "fmuTemplate.h"

// define all model variables and their value references
// conventions used here:
// - if x is a variable, then macro x_ is its variable reference
// - the vr of a variable is its index in array  r, i, b or s
// - if k is the vr of a real state, then k+1 is the vr of its derivative
#define x_     0
#define der_x_ 1
#define k_     2

// define state vector as vector of value references
#define STATES { x_ }

// Linux: Functions in this file are declared to be static so
// that when the fmu_* method invokes one of these methods, then
// it gets the definition in the same shared library instead
// of getting the method with the same name in a previously
// loaded shared library.

// called by fmiInstantiateModel
// Set values for all variables that define a start value
// Settings used unless changed by fmiSetX before fmiInitialize
static void setStartValues(ModelInstance *comp) {
    comp->r[x_] = 1;
    comp->r[k_] = 1;
}

// called by fmiInitialize() after setting eventInfo to defaults
// Used to set the first time event, if any.
static void initialize(ModelInstance* comp, fmiEventInfo* eventInfo) {
}

// called by fmiGetReal, fmiGetContinuousStates and fmiGetDerivatives
static fmiReal getReal(ModelInstance* comp, fmiValueReference vr){
    switch (vr) {
        case x_     : return comp->r[x_];
        case der_x_ : return - comp->r[k_] * comp->r[x_];
        case k_     : return comp->r[k_];
        default: return 0;
    }
}

// Used to set the next time event, if any.
static void eventUpdate(fmiComponent comp, fmiEventInfo* eventInfo) {
} 

// include code that implements the FMI based on the above definitions
#include "fmuTemplate.c"


