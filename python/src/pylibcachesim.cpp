#include <pybind11/pybind11.h>
#include "libCacheSim.h"

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

// int add(int i, int j) {
//     return i + j;
// }

namespace py = pybind11;

PYBIND11_MODULE(_libcachesim, m) {
    m.doc() = R"pbdoc(
        libCacheSim Python bindings
        --------------------------

        .. currentmodule:: libcachesim

        .. autosummary::
           :toctree: _generate

           TODO(haocheng): add meaningful methods
           add
           subtract
    )pbdoc";

    // m.def("add", &add, R"pbdoc(
    //     Add two numbers

    //     Some other explanation about the add function.
    // )pbdoc");

    // m.def("subtract", [](int i, int j) { return i - j; }, R"pbdoc(
    //     Subtract two numbers

    //     Some other explanation about the subtract function.
    // )pbdoc");

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}