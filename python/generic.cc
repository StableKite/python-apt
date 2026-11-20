// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: generic.cc,v 1.1.1.1 2001/02/20 06:32:01 jgg Exp $
/* ######################################################################

   generic - Some handy functions to make integration a tad simpler

   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include "generic.h"
using namespace std;


#include <apt-pkg/error.h>

#ifdef Py_GIL_DISABLED
#include <atomic>
#include <unistd.h>

namespace
{
struct ProcessLock
{
   explicit ProcessLock(pid_t owner) : process(owner) {}

   const pid_t process;
   std::recursive_mutex mutex;
};

struct LockDomain
{
   ProcessLock initial{getpid()};
   std::atomic<ProcessLock *> current{&initial};

   std::recursive_mutex &mutex()
   {
      const auto pid = getpid();
      auto *state = current.load(std::memory_order_acquire);
      if (state->process == pid)
         return state->mutex;

      // A subprocess can be forked from a progress callback. It must not wait
      // on a mutex inherited from a different process.
      auto *replacement = new ProcessLock(pid);
      if (!current.compare_exchange_strong(state, replacement,
                                           std::memory_order_acq_rel)) {
         delete replacement;
         return state->mutex;
      }
      return replacement->mutex;
   }
};

LockDomain native_lock;
LockDomain *active_domain = &native_lock;
const char capsule_name[] = "apt_pkg._native_call_lock";
}

apt_python::NativeCall::NativeCall()
   : lock_(active_domain->mutex(), std::defer_lock)
{
   if (!lock_.try_lock()) {
      // Keep Python threads (including the lock owner) runnable while waiting.
      PyThreadState *state = PyEval_SaveThread();
      try {
         lock_.lock();
      } catch (...) {
         PyEval_RestoreThread(state);
         throw;
      }
      PyEval_RestoreThread(state);
   }
}

int apt_python::export_native_lock(PyObject *module)
{
   PyObject *capsule = PyCapsule_New(&native_lock, capsule_name, nullptr);
   if (!capsule)
      return -1;
   const int result = PyModule_AddObjectRef(module, "_native_call_lock", capsule);
   Py_DECREF(capsule);
   return result;
}

int apt_python::import_native_lock(PyObject *apt_pkg)
{
   PyObject *capsule = PyObject_GetAttrString(apt_pkg, "_native_call_lock");
   if (!capsule)
      return -1;
   auto *domain = static_cast<LockDomain *>(PyCapsule_GetPointer(capsule, capsule_name));
   Py_DECREF(capsule);
   if (!domain)
      return -1;
   active_domain = domain;
   return 0;
}
#endif
									/*}}}*/

// HandleErrors - This moves errors from _error to Python Exceptions	/*{{{*/
// ---------------------------------------------------------------------
/* We throw away all warnings and only propogate the first error. */
PyObject *HandleErrors(PyObject *Res)
{
   string Err;
   int errcnt = 0;
   int wrncnt = 0;
   while (_error->empty() == false)
   {
      string Msg;
      bool Type = _error->PopMessage(Msg);
      if (errcnt > 0 || wrncnt > 0)
         Err.append(", ");
      Err.append((Type == true ? "E:" : "W:"));
      Err.append(Msg);
      if (Type)
          ++errcnt;
      else
          ++wrncnt;
   }
   if (errcnt > 0)
   {
       PyErr_SetString(PyAptError,Err.c_str());
       goto err;
   }
   else if (wrncnt > 0)
   {
       if (PyErr_WarnEx(PyAptWarning, Err.c_str(), 1) == -1)
           goto err;
   }

   return Res;
err:
   if (Res != 0)
      Py_DECREF(Res);

    return nullptr;
}

									/*}}}*/
// TupleToArgv - Convert an immutable argument snapshot to argv		/*{{{*/
// ---------------------------------------------------------------------
bool TupleToArgv(PyObject *tuple, std::vector<const char *> &argv, bool null_terminated)
{
   const auto size = PyTuple_GET_SIZE(tuple);
   argv.reserve(size + (null_terminated ? 1 : 0));
   for (Py_ssize_t i = 0; i < size; ++i)
   {
      const char *value = PyObject_AsString(PyTuple_GET_ITEM(tuple, i));
      if (!value)
         return false;
      argv.push_back(value);
   }
   if (null_terminated)
      argv.push_back(nullptr);
   return true;
}
									/*}}}*/
// CharCharToList - Inverse of the above				/*{{{*/
// ---------------------------------------------------------------------
/* Zero size indicates the list is Null terminated. */
PyObject *CharCharToList(const char **List,unsigned long Size)
{
   if (Size == 0)
   {
      for (const char **I = List; *I != 0; I++)
	 Size++;
   }

   // Convert the whole configuration space into a list
   PyObject *PList = PyList_New(Size);
   for (unsigned long I = 0; I != Size; I++, List++)
      PyList_SetItem(PList,I,CppPyString(*List));

   return PList;
}
									/*}}}*/

int PyApt_Filename::init(PyObject *object)
{
   this->object = NULL;
   this->path = NULL;

#if PY_MAJOR_VERSION < 3 || (PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION < 2)
   this->path = PyObject_AsString(object);
   return this->path ? 1 : 0;
#else
   if (PyUnicode_Check(object)) {
      object = PyUnicode_EncodeFSDefault(object);
   } else if (PyBytes_Check(object)) {
      Py_INCREF(object);
   } else {
      return 0;
   }

   this->object = object;
   this->path = PyBytes_AS_STRING(this->object);
   return 1;
#endif
}
