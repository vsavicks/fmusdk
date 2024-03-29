/* ---------------------------------------------------------------------------*
 * Sample implementation of an FMU 
 * This demonstrates the use of all FMU variable types.
 * (c) 2010 QTronic GmbH 
 * ---------------------------------------------------------------------------*/

// define class name and unique id
#define MODEL_IDENTIFIER values
#define MODEL_GUID "{8c4e810f-3df3-4a00-8276-176fa3c9f004}"

// define model size
#define NUMBER_OF_REALS 2
#define NUMBER_OF_INTEGERS 2
#define NUMBER_OF_BOOLEANS 2
#define NUMBER_OF_STRINGS 2
#define NUMBER_OF_STATES 1
#define NUMBER_OF_EVENT_INDICATORS 0

// include fmu header files, typedefs and macros
#include "fmuTemplate.h"

// define all model variables and their value references
// conventions used here:
// - if x is a variable, then macro x_ is its variable reference
// - the vr of a variable is its index in array  r, i, b or s
// - if k is the vr of a real state, then k+1 is the vr of its derivative
#define x_          0
#define der_x_      1
#define int_in_     0
#define int_out_    1
#define bool_in_    0
#define bool_out_   1
#define string_in_  0
#define string_out_ 1

// define state vector as vector of value references
#define STATES { x_ }

const char* month[] = {
    "jan","feb","march","april","may","june","july",
    "august","sept","october","november","december"
};

static fmiStatus setString(fmiComponent comp, fmiValueReference vr, fmiString value);

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
    comp->i[int_in_] = 2;
    comp->i[int_out_] = 0;
    comp->b[bool_in_] = fmiTrue;
    comp->b[bool_out_] = fmiFalse;
    setString(comp, string_in_, "a string");
  printf("values.c setStartValues 90\n");
    setString(comp, string_out_, month[0]);
  printf("values.c setStartValues end\n");
}

// called by fmiInitialize() after setting eventInfo to defaults
// Used to set the first time event, if any.
static void initialize(ModelInstance* comp, fmiEventInfo* eventInfo) {
    eventInfo->upcomingTimeEvent   = fmiTrue;
    eventInfo->nextEventTime       = 1 + comp->time;
}

// called by fmiGetReal, fmiGetContinuousStates and fmiGetDerivatives
static fmiReal getReal(ModelInstance* comp, fmiValueReference vr){
    switch (vr) {
        case x_     : return   comp->r[x_];
        case der_x_ : return - comp->r[x_];
        default: return 0;
    }
}

// called by fmiEventUpdate() after setting eventInfo to defaults
static void eventUpdate(ModelInstance* comp, fmiEventInfo* eventInfo) {
    eventInfo->upcomingTimeEvent   = fmiTrue;
    eventInfo->nextEventTime       = 1 + comp->time;
    comp->i[int_out_] += 1;
    comp->b[bool_out_] = !comp->b[bool_out_];
    if (comp->i[int_out_]<12) setString(comp, string_out_, month[comp->i[int_out_]]);
    else eventInfo->terminateSimulation = fmiTrue;
} 

// include code that implements the FMI based on the above definitions
#include "fmuTemplate.c"

