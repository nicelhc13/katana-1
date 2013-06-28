/** Galois barrier -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2012, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Description
 *
 * Fast Barrier
 *
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 * @author Andrew Lenharth <andrew@lenharth.org>
 */

#include "Galois/Runtime/PerThreadStorage.h"
#include "Galois/Runtime/Barrier.h"
#include "Galois/Runtime/ActiveThreads.h"
#include "Galois/Runtime/ll/CompilerSpecific.h"
#include "Galois/Runtime/Network.h"
#include "Galois/Runtime/Directory.h"

#include <pthread.h>

#include <cstdlib>
#include <cstdio>

class PthreadBarrier {
  pthread_barrier_t bar;

  void checkResults(int val) {
    if (val) {
      perror("PTHREADS: ");
      assert(0 && "PThread check");
      abort();
    }
  }

public:
  PthreadBarrier() {
    //uninitialized barriers block a lot of threads to help with debugging
    int rc = pthread_barrier_init(&bar, 0, ~0);
    checkResults(rc);
  }
  
  PthreadBarrier(unsigned int val) {
    int rc = pthread_barrier_init(&bar, 0, val);
    checkResults(rc);
  }

  virtual ~PthreadBarrier() {
    int rc = pthread_barrier_destroy(&bar);
    checkResults(rc);
  }

  virtual void reinit(int val) {
    int rc = pthread_barrier_destroy(&bar);
    checkResults(rc);
    rc = pthread_barrier_init(&bar, 0, val);
    checkResults(rc);
  }

  virtual void wait() {
    int rc = pthread_barrier_wait(&bar);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD)
      checkResults(rc);
  }
};

class MCSBarrier :public Galois::Runtime::Barrier {
  struct treenode {
    //vpid is Galois::Runtime::LL::getTID()
    volatile bool* parentpointer; //null of vpid == 0
    volatile bool* childpointers[2];
    bool havechild[4];

    volatile bool childnotready[4];
    volatile bool parentsense;
    bool sense;
  };

  Galois::Runtime::PerThreadStorage<treenode> nodes;
  
  
  void _reinit(unsigned P) {
    for (unsigned i = 0; i < nodes.size(); ++i) {
      treenode& n = *nodes.getRemote(i);
      n.sense = true;
      n.parentsense = false;
      for (int j = 0; j < 4; ++j)
	n.childnotready[j] = n.havechild[j] = ((4*i+j+1) < P);
      n.parentpointer = (i == 0) ? 0 :
	&nodes.getRemote((i-1)/4)->childnotready[(i-1)%4];
      n.childpointers[0] = ((2*i + 1) >= P) ? 0 :
	&nodes.getRemote(2*i+1)->parentsense;
      n.childpointers[1] = ((2*i + 2) >= P) ? 0 :
	&nodes.getRemote(2*i+2)->parentsense;
    }
  }

public:

  MCSBarrier(unsigned P = Galois::Runtime::activeThreads) {
    _reinit(P);
  }

  virtual void reinit(unsigned val) {
    _reinit(val);
  }

  virtual void wait() {
    treenode& n = *nodes.getLocal();
    while (n.childnotready[0] || n.childnotready[1] || 
	   n.childnotready[2] || n.childnotready[3]) {
      Galois::Runtime::LL::asmPause();
    }
    for (int i = 0; i < 4; ++i)
      n.childnotready[i] = n.havechild[i];
    if (n.parentpointer) {
      //FIXME: make sure the compiler doesn't do a RMW because of the as-if rule
      *n.parentpointer = false;
      while(n.parentsense != n.sense) {
	Galois::Runtime::LL::asmPause();
      }
    }
    //signal children in wakeup tree
    if (n.childpointers[0])
      *n.childpointers[0] = n.sense;
    if (n.childpointers[1])
      *n.childpointers[1] = n.sense;
    n.sense = !n.sense;
  }
};





class TopoBarrier : public Galois::Runtime::Barrier {
  struct treenode {
    //vpid is Galois::Runtime::LL::getTID()

    //package binary tree
    treenode* parentpointer; //null of vpid == 0
    treenode* childpointers[2];

    //waiting values:
    unsigned havechild;
    volatile unsigned childnotready;

    //signal values
    volatile unsigned parentsense;
  };

  Galois::Runtime::PerPackageStorage<treenode> nodes;
  Galois::Runtime::PerThreadStorage<unsigned> sense;

  void _reinit(unsigned P) {
    unsigned pkgs = Galois::Runtime::LL::getMaxPackageForThread(P-1) + 1;
    for (unsigned i = 0; i < pkgs; ++i) {
      treenode& n = *nodes.getRemoteByPkg(i);
      n.childnotready = 0;
      n.havechild = 0;
      for (int j = 0; j < 4; ++j) {
	if ((4*i+j+1) < pkgs) {
	  ++n.childnotready;
	  ++n.havechild;
	}
      }
      for (unsigned j = 0; j < P; ++j)
	if (Galois::Runtime::LL::getPackageForThread(j) == i && !Galois::Runtime::LL::isPackageLeader(j)) {
	  ++n.childnotready;
	  ++n.havechild;
	}
      n.parentpointer = (i == 0) ? 0 : nodes.getRemoteByPkg((i-1)/4);
      n.childpointers[0] = ((2*i + 1) >= pkgs) ? 0 : nodes.getRemoteByPkg(2*i+1);
      n.childpointers[1] = ((2*i + 2) >= pkgs) ? 0 : nodes.getRemoteByPkg(2*i+2);
      n.parentsense = 0;
    }
    for (unsigned i = 0; i < P; ++i)
      *sense.getRemote(i) = 1;
  }

public:
  TopoBarrier(unsigned val = Galois::Runtime::activeThreads) {
    _reinit(val);
  }

  //not safe if any thread is in wait
  virtual void reinit(unsigned val) {
    _reinit(val);
  }

  virtual void wait() {
    unsigned id = Galois::Runtime::LL::getTID();
    treenode& n = *nodes.getLocal();
    unsigned& s = *sense.getLocal();
    bool leader = Galois::Runtime::LL::isPackageLeaderForSelf(id);
    //completion tree
    if (leader) {
      while (n.childnotready) { Galois::Runtime::LL::asmPause(); }
      n.childnotready = n.havechild;
      if (n.parentpointer) {
	__sync_fetch_and_sub(&n.parentpointer->childnotready, 1);
      }
    } else {
      __sync_fetch_and_sub(&n.childnotready, 1);
    }
    
    //wait for signal
    if (id != 0) {
      while (n.parentsense != s) {
	Galois::Runtime::LL::asmPause();
      }
    }
    
    //signal children in wakeup tree
    if (leader) {
      if (n.childpointers[0])
	n.childpointers[0]->parentsense = s;
      if (n.childpointers[1])
	n.childpointers[1]->parentsense = s;
      if (id == 0)
	n.parentsense = s;
    }
    ++s;
  }
};

Galois::Runtime::Barrier::~Barrier() {}

// void TopoBarrier::dump() {
//   unsigned pkgs = LL::getMaxPackages();
//   for (unsigned i = 0; i < pkgs; ++i) {
//     treenode* n = nodes.getRemoteByPkg(i);
//     std::cerr << n << " " << n->parentpointer << " " << n->childpointers[0] << " " << n->childpointers[1] << " " << n->havechild << " " << n->childnotready << " " << n->parentsense << "\n";
//   }
//   for (unsigned i = 0; i < sense.size(); ++i) {
//     std::cerr << *sense.getRemote(i) << " ";
//   }
//   std::cerr << "\n";

// }

class StupidDistBarrier;
static StupidDistBarrier& getDistBarrier();

class StupidDistBarrier : public Galois::Runtime::Barrier {

  std::atomic<unsigned> gsense;
  Galois::Runtime::PerThreadStorage<unsigned> sense;
  std::atomic<int> count;

public:
  
  StupidDistBarrier() : gsense(0), count(0) {
    for (unsigned x = 0; x < sense.size(); ++x)
      *sense.getRemote(x) = 1;
  }
  
  void reinit(unsigned val) {
    for (unsigned x = 0; x < sense.size(); ++x)
      *sense.getRemote(x) = 1;
    assert(count == 0);
    gsense = 0;
  }
  
  void wait() {
    assert(*sense.getLocal() == gsense + 1);
    //notify the world
    Galois::Runtime::SendBuffer b;
    Galois::Runtime::getSystemNetworkInterface().broadcast(broadcastLandingPad, b);
    //broadcast skips us
    --count;
    
    //wait for barrier
    if (Galois::Runtime::LL::getTID() == 0) {
      count += Galois::Runtime::networkHostNum * Galois::Runtime::activeThreads;
      while (count > 0)
        Galois::Runtime::doNetworkWork();
      //passed barrier, notify local
      ++gsense;
    } else {
      while (*sense.getLocal() != gsense)
        Galois::Runtime::LL::asmPause();
    }
    //continue
    ++(*sense.getLocal());
    // there's a possibility that one of the thread's broadcast
    // message wasn't communicated
    if (Galois::Runtime::LL::getTID() == 0)
      Galois::Runtime::doNetworkWork();
  }

  static void broadcastLandingPad(Galois::Runtime::RecvBuffer&) {
    --getDistBarrier().count;
    //    std::cout << "BLP: " << Galois::Runtime::networkHostID << " " << getDistBarrier().count << "\n";
  }
  
  void dump() {
    printf("sense");
    for (unsigned x = 0; x < sense.size(); ++x)
      printf(" %d", *sense.getRemote(x));
    printf("\n");
    printf("count %d, gsense %d\n", (int)count, (unsigned)gsense);
  }
  
};

static StupidDistBarrier& getDistBarrier() {
  static StupidDistBarrier b;
  return b;
}


Galois::Runtime::Barrier& Galois::Runtime::getSystemBarrier() {
  static unsigned num = ~0;
  if (networkHostNum == 1) {
    static TopoBarrier b;
    if (activeThreads != num) {
      num = activeThreads;
      b.reinit(num);
    }
    return b;
  } else {
    if (activeThreads != num) {
      num = activeThreads;
      getDistBarrier().reinit(num);
    }
    return getDistBarrier();
  }
}
