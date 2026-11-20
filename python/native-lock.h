#ifndef PYTHON_APT_NATIVE_LOCK_H
#define PYTHON_APT_NATIVE_LOCK_H

#include <Python.h>

#ifdef Py_GIL_DISABLED
#include <cstddef>
#include <mutex>

namespace apt_python
{
// libapt's configuration and system objects are shared by both extension
// modules. Native calls on a Python progress callback may reenter on the same
// thread, so all entry points use the same recursive lock.
class NativeCall
{
public:
   NativeCall();

private:
   std::unique_lock<std::recursive_mutex> lock_;
};

int export_native_lock(PyObject *module);
int import_native_lock(PyObject *apt_pkg);

template <auto Function>
struct LockedCallback;

template <>
struct LockedCallback<0>
{
   static constexpr std::nullptr_t call = nullptr;
};

template <class Result, class... Arguments, Result (*Function)(Arguments...)>
struct LockedCallback<Function>
{
   static Result call(Arguments... arguments)
   {
      NativeCall lock;
      return Function(arguments...);
   }
};
}

#define APT_NATIVE_CALL(...) (apt_python::LockedCallback<__VA_ARGS__>::call)
#else
#define APT_NATIVE_CALL(...) (__VA_ARGS__)
#endif

#endif
