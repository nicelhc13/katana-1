from libc.stdint cimport uint64_t
from libcpp.memory cimport make_shared, shared_ptr, unique_ptr
from libcpp.string cimport string
from libcpp.utility cimport move

from . cimport datastructures

from . import datastructures

from katana.cpp.libgalois.graphs cimport Graph as CGraph
from katana.cpp.libsupport.result cimport Result, raise_error_code
from katana.local._graph cimport Graph, TxnContext, handle_result_PropertyGraph


cdef CGraph.GraphComponents handle_result_GraphComponents(Result[CGraph.GraphComponents] res) nogil except *:
    if not res.has_value():
        with gil:
            raise_error_code(res.error())
    return move(res.value())


def from_csr(edge_indices, edge_destinations):
    """
    Create a new `Graph` from a raw Compressed Sparse Row representation.

    :param edge_indices: The indicies of the first edge for each node in the destinations vector.
    :type edge_indices: `numpy.ndarray` or another type supporting the buffer protocol. Element type must be an
        integer.
    :param edge_destinations: The destinations of edges in the new graph.
    :type edge_destinations: `numpy.ndarray` or another type supporting the buffer protocol. Element type must be an
        integer.
    :returns: the new :py:class:`~katana.local.Graph`
    """
    cdef datastructures.NUMAArray_uint64_t edge_indices_numa = datastructures.NUMAArray_uint64_t(
        len(edge_indices), datastructures.AllocationPolicy.INTERLEAVED
    )
    cdef datastructures.NUMAArray_uint32_t edge_destinations_numa = datastructures.NUMAArray_uint32_t(
        len(edge_destinations), datastructures.AllocationPolicy.INTERLEAVED
    )

    edge_indices_numa.as_numpy()[:] = edge_indices
    edge_destinations_numa.as_numpy()[:] = edge_destinations

    with nogil:
        pg = handle_result_PropertyGraph( CGraph._PropertyGraph.MakeFromTopo(
             CGraph.GraphTopology(move(edge_indices_numa.underlying), move(edge_destinations_numa.underlying))
             ))
    return Graph.make(pg)
