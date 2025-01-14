#ifndef KATANA_LIBKATANAPYTHONNATIVE_KATANA_PYTHON_ERRORHANDLING_H_
#define KATANA_LIBKATANAPYTHONNATIVE_KATANA_PYTHON_ERRORHANDLING_H_

#include <katana/Result.h>
#include <pybind11/pybind11.h>

namespace pybind11 {
namespace detail {

/// Cast an object to python unless it's already a python object.
template <typename T>
pybind11::object
cast_if_needed(T&& v, return_value_policy policy, handle parent) {
  return pybind11::cast(std::move(v), policy, parent);
}

template <>
inline pybind11::object
cast_if_needed<pybind11::object>(
    pybind11::object&& v, return_value_policy, handle) {
  return std::move(v);
}

/// Automatic cast from Result<T> to T raising a Python exception if the Result
/// is a failure.
template <typename T>
struct type_caster<katana::Result<T>> {
public:
  PYBIND11_TYPE_CASTER(
      katana::Result<T>, _("Result[") + make_caster<T>::name + _("]"));

  bool load(handle, bool) {
    // Conversion always fails since result values cannot originate in Python.
    return false;
  }

  static handle cast(
      katana::Result<T> src, return_value_policy policy, handle parent) {
    if (src) {
      // Must release the object reference (count) to the interpreter when
      // returning. Otherwise, the py::object will decref the when it goes out
      // of scope leaving the interpeter with no reference count.
      return cast_if_needed(std::move(src.value()), policy, parent).release();
    } else {
      std::ostringstream ss;
      src.error().Write(ss);
      auto code = src.error().error_code();
      pybind11::object error_type;
      try {
        auto katana_module = pybind11::module_::import("katana");
        error_type = katana_module.attr(code.category().name());
        PyErr_SetString(error_type.ptr(), ss.str().c_str());
      } catch (pybind11::error_already_set& eas) {
        ss << " (error code category is " << code.category().name()
           << " which does not have a custom exception class)";
        error_type = pybind11::reinterpret_borrow<object>(PyExc_RuntimeError);
        pybind11::raise_from(eas, error_type.ptr(), ss.str().c_str());
      }
      throw pybind11::error_already_set();
    }
  }
};

}  // namespace detail
}  // namespace pybind11

#endif
