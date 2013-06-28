#include "Galois/config.h"
#include "Galois/Runtime/Sampling.h"
#include "Galois/Runtime/Support.h"
#include "Galois/Runtime/ll/EnvCheck.h"
#include <cstdlib>

#ifdef GALOIS_USE_EXP
#include "Galois/Runtime/Network.h"
#endif

static void endPeriod() {
  int val;
  if (Galois::Runtime::LL::EnvCheck("GALOIS_EXIT_AFTER_SAMPLING", val)) {
    exit(val);
  }
}

static void beginPeriod() {
  int val;
  if (Galois::Runtime::LL::EnvCheck("GALOIS_EXIT_BEFORE_SAMPLING", val)) {
    exit(val);
  }
}

#ifdef GALOIS_USE_VTUNE
#include "ittnotify.h"
#include "Galois/Runtime/ll/TID.h"

static bool isOn;
static void vtuneBegin() {
  if (!isOn && Galois::Runtime::LL::getTID() == 0)
    __itt_resume();
  isOn = true;
}

static void vtuneEnd() {
  if (isOn && Galois::Runtime::LL::getTID() == 0)
    __itt_pause();
  isOn = false;
}

#else
static void vtuneBegin() {}
static void vtuneEnd() {}
#endif

#ifdef GALOIS_USE_PAPI
extern "C" {
#include <papi.h>
#include <papiStdEventDefs.h>
}

#include <iostream>

static bool isInit = false;
static int papiEventSet = PAPI_NULL;
static long_long papiResults[2];

static int papiEvents[2] = {PAPI_L3_TCA,PAPI_L3_TCM};
static const char* papiNames[2] = {"L3_ACCESSES","L3_MISSES"};

//static int papiEvents[2] = {PAPI_TOT_INS, PAPI_TOT_CYC};
//static const char* papiNames[2] = {"Instructions", "Cycles"};

static void handle_error(int retval) {
  std::cout << "PAPI error " << retval << " " << PAPI_strerror(retval) << "\n";
  abort();
}

static void papiBegin() {
  int rv;
  // Init library
  if (!isInit) {
    rv = PAPI_library_init(PAPI_VER_CURRENT);
    if (rv != PAPI_VER_CURRENT && rv < 0) {
      std::cout << "PAPI library version mismatch!\n";
      abort();
    }
    if (rv < 0) handle_error(rv);
    isInit = true;
  }
  //setup counters
  /* Create the Event Set */
  if ((rv = PAPI_create_eventset(&papiEventSet)) != PAPI_OK)
    handle_error(rv);
  if ((rv = PAPI_add_events(papiEventSet, papiEvents, 2)) != PAPI_OK)
    handle_error(rv);
  /* Start counting events in the Event Set */
  if ((rv = PAPI_start(papiEventSet)) != PAPI_OK)
    handle_error(rv);
}

static void papiEnd() {
  int rv;

  /* get the values */
  if ((rv = PAPI_stop(papiEventSet, papiResults)) != PAPI_OK)
    handle_error(rv);

  /* Remove all events in the eventset */
  if ((rv = PAPI_cleanup_eventset(papiEventSet)) != PAPI_OK)
    handle_error(rv);
  
  /* Free all memory and data structures, EventSet must be empty. */
  if ((rv = PAPI_destroy_eventset(&papiEventSet)) != PAPI_OK)
    handle_error(rv);
}

static void papiReport(const char* loopname) {
  Galois::Runtime::reportStat(loopname, papiNames[0], papiResults[0]);
  Galois::Runtime::reportStat(loopname, papiNames[1], papiResults[1]);
}

#else
static void papiBegin() {}
static void papiEnd() {}
static void papiReport(const char* loopname) {}
#endif

#ifdef GALOIS_USE_EXP
using namespace Galois::Runtime;
static void beginSampling_landing_pad(RecvBuffer& buf) {
  beginPeriod();
  papiBegin();
  vtuneBegin();
}

static void endSampling_landing_pad(RecvBuffer& buf) {
  vtuneEnd();
  papiEnd();
  endPeriod();
}

void Galois::Runtime::beginSampling() {
  if (networkHostNum > 1) {
    SendBuffer b;
    getSystemNetworkInterface().broadcast(beginSampling_landing_pad, b);
  }
  beginPeriod();
  papiBegin();
  vtuneBegin();
}

void Galois::Runtime::endSampling() {
  if (networkHostNum > 1) {
    SendBuffer b;
    getSystemNetworkInterface().broadcast(endSampling_landing_pad, b);
  }
  vtuneEnd();
  papiEnd();
  endPeriod();
}
#else
void Galois::Runtime::beginSampling() {
  beginPeriod();
  papiBegin();
  vtuneBegin();
}

void Galois::Runtime::endSampling() {
  vtuneEnd();
  papiEnd();
  endPeriod();
}
#endif

void Galois::Runtime::reportSampling(const char* loopname) {
  papiReport(loopname);
}
