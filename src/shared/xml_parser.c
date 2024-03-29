/* ------------------------------------------------------------------------- 
 * xml_Parser.c
 * A parser for file modelVariables.xml of an FMU.
 * The parser creates an AST (abstract syntax tree) for a given XML file.
 * The root node of the AST is of type ModelDescription.
 * Validation already performed by this parser
 * - check for match of open/close elements (performed by Expat)
 * - ceck element, attribute and enum value names, all case sensitive
 * - check for each element that is has the expected parent element
 * - check for correct sequence of elements
 * - check that all decalaredType values reference an existing Type
 * Validation to be performed by this parser
 * - check for each attribute value that it is of the expected type 
 * - check that required attributes are present  
 * - check that dependencies are only declared for outputs and
 *   refer only to inputs
 * Author: Jakob Mauss
 * Copyright 2011 QTronic GmbH. All rights reserved. 
 * -------------------------------------------------------------------------*/

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "xml_parser.h"

const char *elmNames[SIZEOF_ELM] = { 
    "fmiModelDescription","UnitDefinitions","BaseUnit","DisplayUnitDefinition","TypeDefinitions",
    "Type","RealType","IntegerType","BooleanType","StringType","EnumerationType","Item",
     "DefaultExperiment","VendorAnnotations","Tool","Annotation", "ModelVariables","ScalarVariable",
     "DirectDependency","Name","Real","Integer","Boolean","String","Enumeration",
     "Implementation","CoSimulation_StandAlone","CoSimulation_Tool","Model","File","Capabilities",

    // component graph
    "Graph","Components","Component","Inputs","Outputs","Port","Connections","Connection"
};

const char *attNames[SIZEOF_ATT] = {
    "fmiVersion","displayUnit","gain","offset","unit","name","description","quantity", "relativeQuantity",
    "min","max","nominal","declaredType","start","fixed","startTime","stopTime","tolerance","value",
    "valueReference","variability","causality","alias", "modelName","modelIdentifier","guid","author",
    "version","generationTool","generationDateAndTime","variableNamingConvention","numberOfContinuousStates",
    "numberOfEventIndicators","input",
    "canHandleVariableCommunicationStepSize","canHandleEvents","canRejectSteps","canInterpolateInputs",
    "maxOutputDerivativeOrder","canRunAsynchronuously","canSignalEvents","canBeInstantiatedOnlyOncePerProcess",
    "canNotUseMemoryManagementFunctions","file","entryPoint","manualStart","type",

    // component graph
    "connection","fmuPath"
};

const char *enuNames[SIZEOF_ENU] = {
    "flat","structured","constant","parameter","discrete","continuous",
    "input","output", "internal","none","noAlias","alias","negatedAlias",

    // component graph
    "Boolean","Integer","Real","String"
};

#define ANY_TYPE -1
#define XMLBUFSIZE 1024
char text[XMLBUFSIZE];       // XML file is parsed in chunks of length XMLBUFZIZE
XML_Parser parser = NULL;    // non-NULL during parsing
Stack* stack = NULL;         // the parser stack
char* data = NULL;           // buffer that holds element content, see handleData
int skipData=0;              // 1 to ignore element content, 0 when recordig content

// ------------------------------------------------------------------------- 
// Low-level functions for inspecting the model description 

const char* getString(void* element, Att a){
    Element* e = (Element*)element;
    const char** attr = e->attributes;
    int i;
    for (i=0; i<e->n; i+=2)
        if (attr[i]==attNames[a]) return attr[i+1];
    return NULL;
}

double getDouble(void* element, Att a, ValueStatus* vs){
    double d = 0;
    const char* value = getString(element, a);
    if (!value) { *vs=valueMissing; return d; }
    *vs = (1==sscanf(value, "%lf", &d)) ? valueDefined : valueIllegal;
    return d;    
}

// getInt() is also used to retrieve Enumeration values from XML,
// e.g. the start value for a variable of user-defined enumeration type.
int getInt(void* element, Att a, ValueStatus* vs){
    int n = 0;
    const char* value = getString(element, a);
    if (!value) { *vs=valueMissing; return n; }
    *vs = (1==sscanf(value, "%d", &n)) ? valueDefined : valueIllegal;
    return n;    
}

unsigned int getUInt(void* element, Att a, ValueStatus* vs){
    unsigned int u = -1;
    const char* value = getString(element, a);
    if (!value) { *vs=valueMissing; return u; }
    *vs = (1==sscanf(value, "%u", &u)) ? valueDefined : valueIllegal;
    return u;    
}

char getBoolean(void* element, Att a, ValueStatus* vs){
    const char* value = getString(element, a);
    if (!value) { *vs=valueMissing; return 0; };
    *vs = valueDefined;
    if (!strcmp(value, "true")) return 1;
    if (!strcmp(value, "false")) return 0;
    *vs = valueIllegal;    
    return 0;
}

static int checkEnumValue(const char* enu);

// Retrieve the value of the given built-in enum attribute.
// If the value is missing, this is marked in the ValueStatus
// and the corresponding default is returned.
// Returns -1 or a globally unique id for the value such that
// enuNames[id] is the string representation of the enum value. 
Enu getEnumValue(void* element, Att a, ValueStatus* vs) {
    const char* value = getString(element, a);
    Enu id /*= valueDefined*/;
    if (!value) { 
        *vs = valueMissing;
        switch (a) {
            case att_variableNamingConvention: return enu_flat;
            case att_variability: return enu_continuous;
            case att_causality: return enu_internal;
            case att_alias: return enu_noAlias;
            default: return -1;
        }
    }
    id = checkEnumValue(value);
    if (id==-1) *vs = valueIllegal; 
    return id;
}

// ------------------------------------------------------------------------- 
// Convenience methods for accessing the model description. 
// Use is only safe after the ast has been successfuly validated.

const char* getModelIdentifier(ModelDescription* md) {
    const char* modelId = getString(md, att_modelIdentifier);
    assert(modelId); // this is a required attribute
    return modelId;
}

int getNumberOfStates(ModelDescription* md) {
    ValueStatus vs;
    int n = getUInt(md, att_numberOfContinuousStates, &vs);
    assert(vs==valueDefined); // this is a required attribute
    return n;
}

int getNumberOfEventIndicators(ModelDescription* md) {
    ValueStatus vs;
    int n = getInt(md, att_numberOfEventIndicators, &vs);
    assert(vs==valueDefined); // this is a required attribute
    return n;
}

// name is a required attribute of ScalarVariable, Type, Item, Annotation, and Tool
const char* getName(void* element) { 
    const char* name = getString(element, att_name);
    assert(name); // this is a required attribute
    return name;
}

// returns one of: input, output, internal, none
// if value is missing, the default internal is returned
Enu getCausality(void* scalarVariable) {
    ValueStatus vs;
    return getEnumValue(scalarVariable, att_causality, &vs);
}

// returns one of constant, parameter, discrete, continuous
// if value is missing, the default continuous is returned
Enu getVariability(void* scalarVariable) {
    ValueStatus vs;
    return getEnumValue(scalarVariable, att_variability, &vs);
}

// returns one of noAlias, alias, negatedAlias
// if value is missing, the default noAlias is returned 
Enu getAlias(void* scalarVariable) {
    ValueStatus vs;
    return getEnumValue(scalarVariable, att_alias, &vs);
}

// the vr is unique only for one of the 4 base data types r,i,b,s and
// may also be fmiUndefinedValueReference = 4294967295 = 0xFFFFFFFF
// here, i means integer or enumeration
fmiValueReference getValueReference(void* scalarVariable) {
    ValueStatus vs;
    fmiValueReference vr = getUInt(scalarVariable, att_valueReference, &vs);
    assert(((Element*)scalarVariable)->type == elm_ScalarVariable);
    assert(vs==valueDefined); // this is a reqired attribute
    return vr;
}

// the name is unique within a fmu
ScalarVariable* getVariableByName(ModelDescription* md, const char* name) {
    int i;
    if (md->modelVariables)
    for (i=0; md->modelVariables[i]; i++){
        ScalarVariable* sv = (ScalarVariable*)md->modelVariables[i];
        if (!strcmp(getName(sv), name)) return sv;
    }
    return NULL;
}

// Enumeration and Integer have the same base type while 
// Real, String, Boolean define own base types.
int sameBaseType(Elm t1, Elm t2){
    return t1==t2 || 
        (t1==elm_Enumeration && t2==elm_Integer) || 
        (t2==elm_Enumeration && t1==elm_Integer);
}

// returns NULL if variable not found or vr==fmiUndefinedValueReference
ScalarVariable* getVariable(ModelDescription* md, fmiValueReference vr, Elm type){
    int i;
    if (md->modelVariables && vr!=fmiUndefinedValueReference)
    for (i=0; md->modelVariables[i]; i++){
        ScalarVariable* sv = (ScalarVariable*)md->modelVariables[i];
        if (sameBaseType(type, sv->typeSpec->type) && getValueReference(sv) == vr) 
            return sv;
    }
    return NULL;
}

Type* getDeclaredType(ModelDescription* md, const char* declaredType){
    int i;
    if (declaredType && md->typeDefinitions)
    for (i=0; md->typeDefinitions[i]; i++){
        Type* tp = (Type*)md->typeDefinitions[i];
        if (!strcmp(declaredType, getName(tp))) return tp;
    }
    return NULL;
}

const char* getString2(ModelDescription* md, void* tp, Att a) {
    Type* type;
    const char* value = getString(tp, a);
    if (value) return value; // found
    // search declared type, if any
    type = getDeclaredType(md, getString(tp, att_declaredType));
    return type ? getString(type->typeSpec, a) : NULL;
}

// Get description from variable or from declared type, or NULL.
const char * getDescription(ModelDescription* md, ScalarVariable* sv) {
    const char* value = getString(sv, att_description);
    Type* type; 
    if (value) return value; // found
    // search declared type, if any
    type = getDeclaredType(md, getString(sv->typeSpec, att_declaredType));
    return type ? getString(type, att_description) : NULL;
}

// Get attribute value from scalar variable given by vr and type, 
// incl. default value provided by declared type, if any.
const char * getVariableAttributeString(ModelDescription* md, 
        fmiValueReference vr, Elm type, Att a){
    const char* value;
    Type* tp; 
    ScalarVariable* sv = getVariable(md, vr, type);
    if (!sv) return NULL;
    value = getString(sv->typeSpec, a);
    if (value) return value; // found
    // search declared type, if any
    tp = getDeclaredType(md, getString(sv->typeSpec, att_declaredType));
    return tp ? getString(tp->typeSpec, a) : NULL;
}

// Get attribute value from scalar variable given by vr and type, 
// incl. default value provided by declared type, if any.
double getVariableAttributeDouble(ModelDescription* md, 
        fmiValueReference vr, Elm type, Att a, ValueStatus* vs){
    double d = 0;
    const char* value = getVariableAttributeString(md, vr, type, a);
    if (!value) { *vs = valueMissing; return d; }
    *vs = (1==sscanf(value, "%lf", &d)) ? valueDefined : valueIllegal;
    return d;    
}

// Get nominal value from real variable or its declared type.
// Return 1, if no nominal value is defined.
double getNominal(ModelDescription* md, fmiValueReference vr){
    ValueStatus vs;
    double nominal = getVariableAttributeDouble(md, vr, elm_Real, att_nominal, &vs);
    return vs==valueDefined ? nominal : 1.0;
}

// ------------------------------------------------------------------------- 
// Various checks that log an error and stop the parser 

// Returns 0 to indicate error
static int checkPointer(const void* ptr){
    if (! ptr) {
        printf("Out of memory\n");
        if (parser) XML_StopParser(parser, XML_FALSE);
        return 0; // error 
    }
    return 1; // success
}

static int checkName(const char* name, const char* kind, const char* array[], int n){
    int i;
    for (i=0; i<n; i++) {
        if (!strcmp(name, array[i])) return i;
    }
    printf("Illegal %s %s\n", kind, name);
    XML_StopParser(parser, XML_FALSE);
    return -1;
}

// Returns -1 to indicate error
static int checkElement(const char* elm){
    return checkName(elm, "element", elmNames, SIZEOF_ELM);
}

// Returns -1 to indicate error
static int checkAttribute(const char* att){
    return checkName(att, "attribute", attNames, SIZEOF_ATT);
}

// Returns -1 to indicate error
static int checkEnumValue(const char* enu){
    return checkName(enu, "enum value", enuNames, SIZEOF_ENU);
}

static void logFatalTypeError(const char* expected, Elm found) {
    printf("Wrong element type, expected %s, found %s\n", 
            expected, elmNames[found]);
    XML_StopParser(parser, XML_FALSE);
}

// Returns 0 to indicate error
// Verify that Element elm is of the given type
static int checkElementType(void* element, Elm e) {
    Element* elm = (Element* )element;
    if (elm->type == e) return 1; // success
    logFatalTypeError(elmNames[e], elm->type);
    return 0; // error    
}

// Returns 0 to indicate error
// Verify that the next stack element exists and is of the given type
// If e==ANY_TYPE, the type check is ommited 
static int checkPeek(Elm e) {
    if (stackIsEmpty(stack)){
        printf("Illegal document structure, expected %s\n", elmNames[e]);
        XML_StopParser(parser, XML_FALSE);
        return 0; // error
    }
    return e==ANY_TYPE ? 1 : checkElementType(stackPeek(stack), e);
}

// Returns NULL to indicate error
// Get the next stack element, it is of the given type.
// If e==ANY_TYPE, the type check is ommited 
static void* checkPop(Elm e){
    return checkPeek(e) ? stackPop(stack) : NULL;
}

// ------------------------------------------------------------------------- 
// Helper 

AstNodeType getAstNodeType(Elm e){
    switch (e) {
    case elm_fmiModelDescription: 
        return astModelDescription;
    case elm_Type:
        return astType;
    case elm_ScalarVariable:
        return astScalarVariable;
    case elm_CoSimulation_StandAlone:
    case elm_CoSimulation_Tool:
        return astCoSimulation;
    case elm_BaseUnit:
    case elm_EnumerationType:
    case elm_Tool:
    case elm_UnitDefinitions:
    case elm_TypeDefinitions:
    case elm_VendorAnnotations:
    case elm_ModelVariables:
    case elm_DirectDependency:
    case elm_Model:
        return astListElement;
	
    // component graph
    //////////////////
    case elm_Graph:
        return astGraph;
    case elm_Component:
        return astComponent;
    case elm_Connection:
        return astConnection;
    case elm_Port:
        return astPort;
    case elm_Components:
    case elm_Inputs:
    case elm_Outputs:
    case elm_Connections:
        return astListElement;
    default:
        return astElement; 
    }
}

// Returns 0 to indicate error
// Copies the attr array and all values.
// Replaces all attribute names by constant literal strings.
// Converts the null-terminated array into an array of known size n.
int addAttributes(Element* el, const char** attr) {
    int n, a;
    const char** att = NULL;
    for (n=0; attr[n]; n+=2);
    if (n>0) {
        att = calloc(n, sizeof(char*));
        if (!checkPointer(att)) return 0;
    } 
    for (n=0; attr[n]; n+=2) {
        char* value = strdup(attr[n+1]);
        if (!checkPointer(value)) return 0;
        a = checkAttribute(attr[n]);
        if (a == -1) return 0;  // illegal attribute error
        att[n  ] = attNames[a]; // no heap memory
        att[n+1] = value;       // heap memory
    }
    el->attributes = att; // NULL if n=0
    el->n = n;
    return 1; // success
}

// Returns NULL to indicate error
Element* newElement(Elm type, int size, const char** attr) {
    Element* e = (Element*)calloc(1, size);
    if (!checkPointer(e)) return NULL; 
    e->type = type;
    e->attributes = NULL;
    e->n=0;
    if (!addAttributes(e, attr)) return NULL;
    return e;
}

// ------------------------------------------------------------------------- 
// callback functions called by the XML parser 

// Create and push a new element node
static void XMLCALL startElement(void *context, const char *elm, const char **attr) {
    Elm el;
    void* e;
    int size;
    el = checkElement(elm);
    if (el==-1) return; // error
    skipData = (el != elm_Name); // skip element content for all elements but Name
    switch(getAstNodeType(el)){
        case astElement:          size = sizeof(Element); break;
        case astListElement:      size = sizeof(ListElement); break;
        case astType:             size = sizeof(Type); break;
        case astScalarVariable:   size = sizeof(ScalarVariable); break;
        case astCoSimulation:     size = sizeof(CoSimulation); break;
        case astModelDescription: size = sizeof(ModelDescription); break;

        // component graph
        //////////////////
        case astComponent:      size = sizeof(Component); break;
        case astConnection:     size = sizeof(Connection); break;
        case astPort:           size = sizeof(Port); break;
        case astGraph:          size = sizeof(Graph); break;
	default: assert(0);
    }
    e = newElement(el, size, attr);
    checkPointer(e); 
    stackPush(stack, e);
}

// Pop all elements of the given type from stack and 
// add it to the ListElement that follows.
// The ListElement remains on the stack.
static void popList(Elm e) {
    int n = 0;
    Element** array;
    Element* elm = stackPop(stack);
    while (elm->type == e) {
        elm = stackPop(stack);
        n++;
    }
    stackPush(stack, elm); // push ListElement back to stack
    array = (Element**)stackLastPopedAsArray0(stack, n); // NULL terminated list
    if (getAstNodeType(elm->type)!=astListElement) return; // failure
    ((ListElement*)elm)->list = array;
    return; // success only if list!=NULL    
}

// Pop the children from the stack and
// check for correct type and sequence of children
static void XMLCALL endElement(void *context, const char *elm) {
    Elm el;
    el = checkElement(elm);
    switch(el) { 
        case elm_fmiModelDescription: 
            {
                 ModelDescription* md;
                 ListElement** ud = NULL;     // NULL or list of BaseUnits
                 Type**        td = NULL;     // NULL or list of Types 
                 Element*      de = NULL;     // NULL or DefaultExperiment
                 ListElement** va = NULL;     // NULL or list of Tools
                 ScalarVariable** mv = NULL;  // NULL or list of ScalarVariable
                 CoSimulation *cs = NULL;     // NULL or CoSimulation
                 ListElement* child;

                 child = checkPop(ANY_TYPE);
                 if (child->type == elm_CoSimulation_StandAlone || child->type == elm_CoSimulation_Tool) {
                     cs = (CoSimulation*)child;
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (child->type == elm_ModelVariables){
                     mv = (ScalarVariable**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (child->type == elm_VendorAnnotations){
                     va = (ListElement**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (child->type == elm_DefaultExperiment){
                     de = (Element*)child;
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (child->type == elm_TypeDefinitions){
                     td = (Type**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (child->type == elm_UnitDefinitions){
                     ud = (ListElement**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 // work around bug of SimulationX 3.4 and 3.5 which places Implementation at wrong location 
                 if (!cs && (child->type == elm_CoSimulation_StandAlone || child->type == elm_CoSimulation_Tool)) {
                     cs = (CoSimulation*)child;
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (!checkElementType(child, elm_fmiModelDescription)) return;
                 md = (ModelDescription*)child;
                 md->modelVariables = mv;
                 md->vendorAnnotations = va;
                 md->defaultExperiment = de;
                 md->typeDefinitions = td;
                 md->unitDefinitions = ud;
                 md->cosimulation = cs;
                 stackPush(stack, md);
                 break;
            }
        case elm_Implementation:
            {
                 // replace Implementation element
                 void* cs = checkPop(ANY_TYPE);
                 void* im = checkPop(elm_Implementation);
                 stackPush(stack, cs);
                 free(im);
                 el = ((Element*)cs)->type;
                 break;
            }
        case elm_CoSimulation_StandAlone:  
            {
                 Element* ca = checkPop(elm_Capabilities);
                 CoSimulation* cs = checkPop(elm_CoSimulation_StandAlone);
                 if (!ca || !cs) return;
                 cs->capabilities = ca;
                 stackPush(stack, cs);
                 break;
            }   
        case elm_CoSimulation_Tool:
            {
                 ListElement* mo = checkPop(elm_Model);
                 Element* ca = checkPop(elm_Capabilities);
                 CoSimulation* cs = checkPop(elm_CoSimulation_Tool);
                 if (!ca || !mo || !cs) return;
                 cs->capabilities = ca;
                 cs->model = mo;
                 stackPush(stack, cs);
                 break;
            }   
        case elm_Type:
            {
                Type* tp;
                Element* ts = checkPop(ANY_TYPE);
                if (!ts) return;
                if (!checkPeek(elm_Type)) return;
                tp = (Type*)stackPeek(stack);
                switch (ts->type) {
                    case elm_RealType:
                    case elm_IntegerType:
                    case elm_BooleanType:
                    case elm_StringType:
                    case elm_EnumerationType:
                        break;
                    default:
                         logFatalTypeError("RealType or similar", ts->type);
                         return;
                }
                tp->typeSpec = ts;
                break;
            }
        case elm_ScalarVariable:
            {
                ScalarVariable* sv;
                Element** list = NULL;
                Element* child = checkPop(ANY_TYPE);
                if (!child) return;
                if (child->type==elm_DirectDependency){
                    list = ((ListElement*)child)->list;
                    free(child);
                    child = checkPop(ANY_TYPE);
                    if (!child) return;
                }
                if (!checkPeek(elm_ScalarVariable)) return;
                sv = (ScalarVariable*)stackPeek(stack);
                switch (child->type) {
                    case elm_Real:
                    case elm_Integer:
                    case elm_Boolean:
                    case elm_String:
                    case elm_Enumeration:
                        break;
                    default:
                         logFatalTypeError("Real or similar", child->type);
                         return;
                }
                sv->directDependencies = list;
                sv->typeSpec = child;
                break;
            }
        case elm_ModelVariables:    popList(elm_ScalarVariable); break;
        case elm_VendorAnnotations: popList(elm_Tool);break;
        case elm_Tool:              popList(elm_Annotation); break;
        case elm_TypeDefinitions:   popList(elm_Type); break;
        case elm_EnumerationType:   popList(elm_Item); break;
        case elm_UnitDefinitions:   popList(elm_BaseUnit); break;
        case elm_BaseUnit:          popList(elm_DisplayUnitDefinition); break;
        case elm_DirectDependency:  popList(elm_Name); break;
        case elm_Model:             popList(elm_File); break;
        case elm_Name:
            {
                 // Exception: the name value is represented as element content.
                 // All other values of the XML file are represented using attributes.
                 Element* name = checkPop(elm_Name);
                 if (!name) return;
                 name->n = 2;
                 name->attributes = malloc(2*sizeof(char*));
                 name->attributes[0] = attNames[att_input];
                 name->attributes[1] = data;
                 data = NULL;
                 skipData = 1; // stop recording element content
                 stackPush(stack, name);
                 break;
            }

        // component graph
        //////////////////
        case elm_Graph: 
            {
                 Graph*     graph;
                 Component** comps = NULL;      // list of Components
                 Connection** conns = NULL;     // list of Connections
                 ListElement* child;
				
                 child = checkPop(ANY_TYPE);
                 if (child->type == elm_Connections){
                     conns = (Connection**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (child->type == elm_Components){
                     comps = (Component**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (!checkElementType(child, elm_Graph)) return;
                 graph = (Graph*)child;
                 graph->components = comps;
                 graph->connections = conns;
                 stackPush(stack, graph);
                 break;
            }
        case elm_Component:
            {
                 Component*     component;
                 Port**         ins = NULL;
                 Port**         outs = NULL;
                 ListElement*   child;

                 child = checkPop(ANY_TYPE);
                 if (!child) return;
                 if (child->type == elm_Outputs){
                     outs = (Port**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (child->type == elm_Inputs){
                     ins = (Port**)child->list;
                     free(child);
                     child = checkPop(ANY_TYPE);
                     if (!child) return;
                 }
                 if (!checkElementType(child, elm_Component)) return;
                 component = (Component*)child;
                 component->inputs = ins;
                 component->outputs = outs;
                 component->fmu = NULL;
                 stackPush(stack, component);
                 break;
            }
        case elm_Components:        popList(elm_Component); break;
        case elm_Inputs:            popList(elm_Port); break;
        case elm_Outputs:           popList(elm_Port); break;
        case elm_Connections:       popList(elm_Connection); break;
        case elm_Connection:
            {
                Connection* con = checkPop(elm_Connection);
                if (!con) return;
                con->value = NULL;
                stackPush(stack, con);
                break;
            }
        case elm_Port:
            {
                Port* port = checkPop(elm_Port);
                if (!port) return;
                port->connection = NULL;
                stackPush(stack, port);
                break;
            }

        case -1: return; // illegal element error
        default: // must be a leaf Element
                 assert(getAstNodeType(el)==astElement);
                 break;
    }
    // All children of el removed from the stack.
    // The top element must be of type el now.
    checkPeek(el);
}

// Called to handle element data, e.g. "xy" in <Name>xy</Name>
// Can be called many times, e.g. with "x" and then with "y" in the example above.
// Feature in expat:
// For some reason, if the element data is the empty string (Eg. <a></a>)
// instead of an empty string with len == 0 we get "\n". The workaround is
// to replace this with the empty string whenever we encounter "\n".
void XMLCALL handleData(void *context, const XML_Char *s, int len) {
    int n;
    if (skipData) return;
    if (!data) {
        // start a new data string
        if (len == 1 && s[0] == '\n') {
            data = strdup("");
        } else {
            data = malloc(len + 1);
            strncpy(data, s, len);
            data[len] = '\0';
        }
    }
    else {
        // continue existing string
        n = strlen(data) + len;
        data = realloc(data, n+1);
        strncat(data, s, len);
        data[n] = '\0';
    }
    return;
}

// ------------------------------------------------------------------------- 
// printing
 
static void printList(int indent, void** list);

void printElement(int indent, void* element){
    int i;
    Element* e = (Element*)element;
    if (!e) return;
    // print attributes
    for (i=0; i<indent; i++) printf(" ");
    printf("%s", elmNames[e->type]);
    for (i=0; i<e->n; i+=2) 
        printf(" %s=%s", e->attributes[i], e->attributes[i+1]);
    printf("\n");
    // print child nodes
    indent += 2;
    switch (getAstNodeType(e->type)) {
        case astElement:
            printElement(indent, (void**)(Element*)e);
            break;
        case astListElement:
            printList(indent, (void**)((ListElement*)e)->list);
            break;
        case astScalarVariable:
            printElement(indent, ((Type*)e)->typeSpec);
            printList(indent, (void**)((ScalarVariable*)e)->directDependencies);
            break;
        case astType:
            printElement(indent, ((Type*)e)->typeSpec);
            break;
        case astCoSimulation: {
            CoSimulation* cs = (CoSimulation*)e;
            printElement(indent, cs->capabilities);
            printElement(indent, cs->model);
            break;
        }
        case astModelDescription: {
            ModelDescription *md = (ModelDescription*)e;
            printList(indent, (void**)md->unitDefinitions);
            printList(indent, (void**)md->typeDefinitions);
            printElement(indent, md->defaultExperiment);
            printList(indent, (void**)md->vendorAnnotations);
            printList(indent, (void**)md->modelVariables);
            printElement(indent, md->cosimulation);
            break;
        }

        // component diagram:
        case astComponent: {
            Component* c = (Component*)e;
            printList(indent, (void**)c->inputs);
            printList(indent, (void**)c->outputs);
            break;
        }
        case astConnection: {
            //TODO
            break;
        }
        case astPort: {
            //TODO
            break;
        }
        case astGraph: {
            Graph *g = (Graph*)e;
            printList(indent, (void**)g->components);
	    printList(indent, (void**)g->connections);
            break;
        }
    }
}

static void printList(int indent, void** list){
    int i;
    if (list) for (i=0; list[i]; i++) 
       printElement(indent, list[i]);
}

// ------------------------------------------------------------------------- 
// free memory of the AST

static void freeList(void** list);

void freeElement(void* element){
    int i;
    Element* e = (Element*)element;
    if (!e) return;
    // free attributes
    for (i=0; i<e->n; i+=2) 
        free((void*)e->attributes[i+1]);
    if (e->attributes) free(e->attributes);
    // free child nodes
    switch (getAstNodeType(e->type)) {
        case astElement:
	    // Add an empty case here to avoid warnings from -Wall
	    // The element e gets freed elsewhere, if we free it
	    // here,  we get warnings
	    //freeElement((void**)(Element*)e);
            break;
        case astListElement:
            freeList((void*)((ListElement*)e)->list);
            break;
        case astScalarVariable:
            freeList((void*)((ScalarVariable*)e)->directDependencies);
        case astType:
            freeElement(((Type*)e)->typeSpec);
            break;
        case astCoSimulation: {
            CoSimulation* cs = (CoSimulation*)e;
            freeElement(cs->capabilities);
            freeElement(cs->model);
            break;
        }
        case astModelDescription: {
            ModelDescription* md = (ModelDescription*)e;
            freeList((void*)md->unitDefinitions);
            freeList((void*)md->typeDefinitions);
            freeElement(md->defaultExperiment);
            freeList((void*)md->vendorAnnotations);
            freeList((void*)md->modelVariables);
            freeElement(md->cosimulation);
            break;
       }

        // component diagram:
        case astComponent: {
            Component* nd = (Component*)e;
            freeList((void*)nd->inputs);
            freeList((void*)nd->outputs);
            break;
        }
        case astConnection: {
            free(((Connection*)e)->value);
            break;
        }
        case astPort: {
            //TODO
            break;
        }
        case astGraph: {
            Graph* g = (Graph*)e;
            freeList((void*)g->components);
            freeList((void*)g->connections);
            break;
       }
    }
    // free the struct
    free(e);
}

static void freeList(void** list){
    int i;
    if (!list) return;
    for (i=0; list[i]; i++) 
        freeElement(list[i]);
    free(list);
}

// ------------------------------------------------------------------------- 
// Validation - done after parsing to report all errors 

ModelDescription* validate(ModelDescription* md) {
    int error = 0;
    int i;
    if (md->modelVariables)
    for (i=0; md->modelVariables[i]; i++){
        ScalarVariable* sv = (ScalarVariable*)md->modelVariables[i];
        const char* declaredType = getString(sv->typeSpec, att_declaredType);
        Type* decltype = getDeclaredType(md, declaredType);
        if (declaredType && decltype==NULL) {
            printf("Warning: Declared type %s of variable %s not found in modelDescription.xml\n", declaredType, getName(sv));
            error++;
        }
    }
    if (error) {
        printf("Error: Found %d error in modelDescription.xml\n", error);
        return NULL;
    }
    return md;
}


// ------------------------------------------------------------------------- 
// Graph validation - done after parsing to report all errors


static Connection* getConnectionByName(Graph* graph, const char* connectionName){
    int i;
    if (connectionName && graph->connections)
    for (i=0; graph->connections[i]; i++){
        Connection* con = graph->connections[i];
        if (!strcmp(connectionName, getName(con))) return con;
    }
    return NULL;
}

// returns one of: real, integer, boolean, string, none
static Enu getPortType(void* port) {
    ValueStatus vs;
    return getEnumValue(port, att_type, &vs);
}

// validates port's connection attribute for declared connection
// assigns connection to a port if valid, otherwise increases the error count
static void validatePortConnection(Graph* graph, Port* port, int* error) {
    const char* conName = getString(port, att_connection);  //TODO: add null-validation
    Connection* con = getConnectionByName(graph, conName);
    if (conName && con==NULL) {
        printf("Warning: Declared connection %s of linked port %s not found in connection diagram file\n", conName, getName(port));
        (*error)++;
    } else if (con) {
        // check port type
        // allocate type variable
        if (!con->value) {
            void* value = NULL;
            Enu type = getPortType(port);
            switch (type) {
                case enu_Real:
                    value = calloc(1, sizeof(fmiReal));
                    break;
                case enu_Integer:
                    value = calloc(1, sizeof(fmiInteger));
                    break;
                case enu_Boolean:
                    value = calloc(1, sizeof(fmiBoolean));
                    break;
                case enu_String:
                    value = calloc(1, sizeof(fmiString));
                    break;
                default:
                    printf("Warning: Declared port %s has illegal type %s\n", getName(port), getString(port, att_type));
                    (*error)++;
            }
            if (value) {
                con->value = value;
            } else if (type != -1) {
                printf("Error: Value allocation failed for port %s\n", getName(port));
                (*error)++;
            }
        }
        port->connection = con;
    }
}

// validates the graph for valid port connections
//TODO: add more validation i.e. missing attributes, port types etc.
Graph* validateGraph(Graph* graph) {
    int error = 0;
    int i,n;
    Port** ports;

    for (i=0; graph->components[i]; i++) {
        // check output ports' connections
        ports = graph->components[i]->outputs;
        if (ports) {
            for (n=0; ports[n]; n++) {
                validatePortConnection(graph, ports[n], &error);
            }
        }
        // check input ports' connections
        ports = graph->components[i]->inputs;
        if (ports) {
            for (n=0; ports[n]; n++) {
                validatePortConnection(graph, ports[n], &error);
            }
        }
    }
    if (error) {
        printf("Error: Found %d error(s) in component diagram file\n", error);
        return NULL;
    }
    return graph;
}

// ------------------------------------------------------------------------- 
// Entry function parse() of the XML parser 

static void cleanup(FILE *file) {
    stackFree(stack);
    stack = NULL;
    XML_ParserFree(parser);
    parser = NULL;
    fclose(file);
}

// Returns NULL to indicate failure
// Otherwise, return the root node md of the AST.
// The receiver must call freeElement(md) to release AST memory.
ModelDescription* parse(const char* xmlPath) {
    ModelDescription* md = NULL;
    FILE *file;
    int done = 0;
    stack = stackNew(100, 10);
    if (!checkPointer(stack)) return NULL;  // failure
    parser = XML_ParserCreate(NULL);
    if (!checkPointer(parser)) return NULL;  // failure
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, handleData);
  	file = fopen(xmlPath, "rb");
	if (file == NULL) {
        printf("Cannot open file '%s'\n", xmlPath);
     	XML_ParserFree(parser);
        return NULL; // failure
    }
    while (!done) {
        int n = fread(text, sizeof(char), XMLBUFSIZE, file);
	    if (n != XMLBUFSIZE) done = 1;
        if (!XML_Parse(parser, text, n, done)){
             printf("Parse error in file %s at line %d:\n%s\n", 
                     xmlPath,
                     (int)XML_GetCurrentLineNumber(parser),
                     XML_ErrorString(XML_GetErrorCode(parser)));
             while (! stackIsEmpty(stack)) md = stackPop(stack);
             if (md) freeElement(md);
             cleanup(file);
             return NULL; // failure
        }
    }
    md = stackPop(stack);
    assert(stackIsEmpty(stack));
    cleanup(file);
    //printElement(1, md); // debug
    return validate(md); // success if all refs are valid    
}

Graph* parseGraph(const char* xmlPath) {
    Graph* g = NULL;
    FILE *file;
    int done = 0;
    stack = stackNew(100, 10);
    if (!checkPointer(stack)) return NULL;  // failure
    parser = XML_ParserCreate(NULL);
    if (!checkPointer(parser)) return NULL;  // failure
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, handleData);
  	file = fopen(xmlPath, "rb");
	if (file == NULL) {
        printf("Cannot open file '%s'\n", xmlPath);
     	XML_ParserFree(parser);
        return NULL; // failure
    }
    while (!done) {
        int n = fread(text, sizeof(char), XMLBUFSIZE, file);
	    if (n != XMLBUFSIZE) done = 1;
        if (!XML_Parse(parser, text, n, done)){
             printf("Parse error in file %s at line %d:\n%s\n", 
                     xmlPath,
                     (int)XML_GetCurrentLineNumber(parser),
                     XML_ErrorString(XML_GetErrorCode(parser)));
             while (! stackIsEmpty(stack)) g = stackPop(stack);
             if (g) freeElement(g);
             cleanup(file);
             return NULL; // failure
        }
    }
    g = stackPop(stack);
    assert(stackIsEmpty(stack));
    cleanup(file);
    //printElement(1, md); // debug
    return validateGraph(g); // success if all refs are valid    
}


