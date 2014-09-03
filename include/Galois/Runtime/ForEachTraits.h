/** Traits of the Foreach loop body functor -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
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
 * Traits of the for_each loop body functor.
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#ifndef GALOIS_RUNTIME_FOREACHTRAITS_H
#define GALOIS_RUNTIME_FOREACHTRAITS_H

#include "Galois/TypeTraits.h"

namespace Galois {
namespace Runtime {
namespace {
template<typename FunctionTy>
class ForEachTraits {
  // special_decay of std::ref(t) is T& so apply twice
  typedef typename DEPRECATED::special_decay<typename DEPRECATED::special_decay<FunctionTy>::type>::type Fn;
public:
  enum {
    NeedsStats = !Galois::DEPRECATED::does_not_need_stats<Fn>::value,
    NeedsBreak = Galois::DEPRECATED::needs_parallel_break<Fn>::value,
    NeedsPush = !Galois::DEPRECATED::does_not_need_push<Fn>::value,
    NeedsPIA = Galois::DEPRECATED::needs_per_iter_alloc<Fn>::value,
    NeedsAborts = !Galois::DEPRECATED::does_not_need_aborts<Fn>::value
  };
};

}
}
} // end namespace Galois

#endif // GALOIS_RUNTIME_FOREACHTRAITS_H
