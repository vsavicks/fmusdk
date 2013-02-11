/* ------------------------------------------------------------------------- 
 * main.c
 * Implements simulation of a single FMU instance 
 * that implements the "FMI for Co-Simulation 1.0" interface.
 * Command syntax: see printHelp()
 * Simulates the given FMU from t = 0 .. tEnd with fixed step size h and 
 * writes the computed solution to file 'result.csv'.
 * The CSV file (comma-separated values) may e.g. be plotted using 
 * OpenOffice Calc or Microsoft Excel. 
 * This progamm demonstrates basic use of an FMU.
 * Real applications may use advanced master algorithms to cosimulate 
 * many FMUs, limit the numerical error using error estimation
 * and back-stepping, provide graphical plotting utilities, debug support, 
 * and user control of parameter and start values, or perform a clean
 * error handling (e.g. call freeSlaveInstance when a call to the fmu
 * returns with error). All this is missing here.
 *
 * Revision history
 *  22.08.2011 initial version released in FMU SDK 1.0.2
 *     
 * Free libraries and tools used to implement this simulator:
 *  - header files from the FMU specification
 *  - eXpat 2.0.1 XML parser, see http://expat.sourceforge.net
 *  - 7z.exe 4.57 zip and unzip tool, see http://www.7-zip.org
 * Author: Jakob Mauss
 * Copyright 2011 QTronic GmbH. All rights reserved. 
 * -------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fmi_cs.h"
#include "sim_support.h"

static Graph* loadGraph(const char* graphFileName) {
    Graph* graph;           // component graph
    Component** comps;      // list of components
    Port** ports;           // list of ports (input/output)
    int i,n;                // helpers

    // parse component graph xml
    graph = parseGraph(graphFileName);
    if (!graph) exit(EXIT_FAILURE);

    // load fmu and set ports
    comps = graph->components;
    for (i=0; comps[i]; i++) {
        FMU* fmu = (FMU*)calloc(1, sizeof(FMU));
        if (!fmu) return NULL; //TODO add proper error handling
        char* fmuFileName = getString(comps[i], att_fmuPath);
        loadFMU(fmu, fmuFileName);

        // input ports
        if (comps[i]->inputs) {
            ports = comps[i]->inputs;
            for (n=0; ports[n]; n++) {
                ports[n]->variable = getVariableByName(fmu->modelDescription, getName(ports[n]));
            }
        }

        // output ports
        if (comps[i]->outputs) {
            ports = comps[i]->outputs;
            for (n=0; ports[n]; n++) {
                ports[n]->variable = getVariableByName(fmu->modelDescription, getName(ports[n]));

            }
        }

        comps[i]->fmu = (void*)fmu;
    }

    return graph;
}

// simulate the given FMU using the forward euler method.
// time events are processed by reducing step size to exactly hit tNext.
// state events are checked and fired only at the end of an Euler step. 
// the simulator may therefore miss state events and fires state events typically too late.
static int simulate(Graph* graph, double tEnd, double h, fmiBoolean loggingOn, char separator) {
    double time;
    double tStart = 0;               // start time
    const char* guid;                // global unique id of the fmu
    fmiComponent c;                  // instance of the fmu
    fmiStatus fmiFlag;               // return code of the fmu functions
    const char* fmuLocation = NULL;  // path to the fmu as URL, "file://C:\QTronic\sales"
    const char* mimeType = "application/x-fmu-sharedlibrary"; // denotes tool in case of tool coupling
    fmiReal timeout = 1000;          // wait period in milli seconds, 0 for unlimited wait period"
    fmiBoolean visible = fmiFalse;   // no simulator user interface
    fmiBoolean interactive = fmiFalse; // simulation run without user interaction
    fmiCallbackFunctions callbacks;  // called by the model during simulation
    ModelDescription* md;            // handle to the parsed XML file   
    FMU* fmu;                        // handle to fmu
    int nSteps = 0;
    FILE* file;
    int i,n;
    Port** ports;

    // set callback functions
    callbacks.logger = fmuLogger;
    callbacks.allocateMemory = calloc;
    callbacks.freeMemory = free;
    callbacks.stepFinished = NULL; // fmiDoStep has to be carried out synchronously

    // instantiate slaves
    for (i=0; graph->components[i]; i++) {
        fmu = (FMU*) graph->components[i]->fmu;
        md = fmu->modelDescription;
        guid = getString(md, att_guid);
        c = fmu->instantiateSlave(getModelIdentifier(md), guid, fmuLocation, mimeType, 
                              timeout, visible, interactive, callbacks, loggingOn);
        if (!c) return error("could not instantiate model");
        graph->components[i]->instance = c;
        c = NULL;
    }

    // open result file
    if (!(file=fopen(RESULT_FILE, "w"))) {
        printf("could not write %s because:\n", RESULT_FILE);
        printf("    %s\n", strerror(errno));
        return 0; // failure
    }
    
    // initialise slaves
    // StopTimeDefined=fmiFalse means: ignore value of tEnd
    for (i=0; graph->components[i]; i++) {
        fmu = (FMU*) graph->components[i]->fmu;
        c = graph->components[i]->instance;
        fmiFlag = fmu->initializeSlave(c, tStart, fmiTrue, tEnd);
        if (fmiFlag > fmiWarning)  return error("could not initialize model");
    }

    // output solution for time t0
    outputRow(graph, tStart, file, separator, TRUE);  // output column names
    outputRow(graph, tStart, file, separator, FALSE); // output values

    // enter the simulation loop
    time = tStart;
    while (time < tEnd) {
        // read outputs
        for (i=0; graph->components[i]; i++) {
            fmu = (FMU*) graph->components[i]->fmu;
            c = graph->components[i]->instance;
            ports = graph->components[i]->outputs;
            if (!ports) continue;

            for (n=0; ports[n]; n++) {
                ScalarVariable* sv = ports[n]->variable;
                fmiValueReference vr = getValueReference(sv);
                void* value = ports[n]->connection->value;
                switch (sv->typeSpec->type) {
                    case elm_Real:
                        fmu->getReal(c, &vr, 1, (fmiReal*) value);
                        break;
                    case elm_Integer:
                        fmu->getInteger(c, &vr, 1, (fmiInteger*) value);
                        break;
                    case elm_Boolean:
                        fmu->getBoolean(c, &vr, 1, (fmiBoolean*) value);
                        break;
                    case elm_String:
                        fmu->getString(c, &vr, 1, (fmiString*) value);
                        break;
                }
            }
        }

        // set inputs
        for (i=0; graph->components[i]; i++) {
            fmu = (FMU*) graph->components[i]->fmu;
            c = graph->components[i]->instance;
            ports = graph->components[i]->inputs;
            if (!ports) continue;

            for (n=0; ports[n]; n++) {
                ScalarVariable* sv = ports[n]->variable;
                fmiValueReference vr = getValueReference(sv);
                void* value = ports[n]->connection->value;
                switch (sv->typeSpec->type) {
                    case elm_Real:
                        fmu->setReal(c, &vr, 1, (fmiReal*) value);
                        break;
                    case elm_Integer:
                        fmu->setInteger(c, &vr, 1, (fmiInteger*) value);
                        break;
                    case elm_Boolean:
                        fmu->setBoolean(c, &vr, 1, (fmiBoolean*) value);
                        break;
                    case elm_String:
                        fmu->setString(c, &vr, 1, (fmiString*) value);
                        break;
                }
            }
        }
    
        for (i=0; graph->components[i]; i++) {
            fmu = (FMU*) graph->components[i]->fmu;
            c = graph->components[i]->instance;

            // do simulation step
            fmiFlag = fmu->doStep(c, time, h, fmiTrue);
            if (fmiFlag != fmiOK)  return error("could not complete simulation of the model");
        }

        // increment master time
        time += h;
        outputRow(graph, time, file, separator, FALSE); // output values for this step
        nSteps++;
    }
    
    // end simulation
    for (i=0; graph->components[i]; i++) {
        fmu = (FMU*)graph->components[i]->fmu;
        c = graph->components[i]->instance;
        fmiFlag = fmu->terminateSlave(c);
        fmu->freeSlaveInstance(c);
    }
  
    // print simulation summary 
    printf("Simulation from %g to %g terminated successful\n", tStart, tEnd);
    printf("  steps ............ %d\n", nSteps);
    printf("  fixed step size .. %g\n", h);
    return 1; // success
}

int main(int argc, char *argv[]) {
    Graph* graph;
    char* graphFileName;
    
    // parse command line arguments and load the FMU
    double tEnd = 1.0;
    double h=0.1;
    int loggingOn = 0;
    char csv_separator = ';';
    parseArguments(argc, argv, &graphFileName, &tEnd, &h, &loggingOn, &csv_separator);
    graph = loadGraph(graphFileName);

    // run the simulation
    printf("FMU Simulator: run configuration '%s' from t=0..%g with step size h=%g, loggingOn=%d, csv separator='%c'\n", 
            graphFileName, tEnd, h, loggingOn, csv_separator);
    simulate(graph, tEnd, h, loggingOn, csv_separator);
    printf("CSV file '%s' written\n", RESULT_FILE);

    // release FMU 
    int i;
    Component** comps = graph->components;
    for (i=0; comps[i]; i++) {
#ifdef _MSC_VER
        FreeLibrary(((FMU*)comps[i]->fmu)->dllHandle);
#else
        dlclose(((FMU*)comps[i]->fmu)->dllHandle);
#endif
        freeElement(((FMU*)comps[i]->fmu)->modelDescription);
        free(comps[i]->fmu);
    }
    freeElement(graph);
    return EXIT_SUCCESS;
}

