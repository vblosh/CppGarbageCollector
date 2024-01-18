#pragma once

#include <assert.h>
#include "IRootsRegistry.h"
#include "GCObject.h"

namespace cppgc
{
    class GCObjectRootPtrBase
    {
        IRootsRegistry& _rootsRegistry;

    protected:
        GCObject* _pObject;

    public:
        GCObjectRootPtrBase(IRootsRegistry& rootsRegistry) : _rootsRegistry(rootsRegistry), _pObject(nullptr)
        {
            _rootsRegistry.addRoot(this);
        }

        ~GCObjectRootPtrBase()
        {
            _rootsRegistry.removeRoot(this);
        }

        GCObjectRootPtrBase(const GCObjectRootPtrBase& rhs) : _rootsRegistry(rhs._rootsRegistry), _pObject(rhs._pObject)
        {
            _rootsRegistry.addRoot(this);
        }

        GCObjectRootPtrBase& operator=(const GCObjectRootPtrBase& rhs)
        {
            assert(&_rootsRegistry == &rhs._rootsRegistry);
            if (&rhs != this)
            {
                _pObject = rhs._pObject;
            }
            return *this;
        }

        GCObjectRootPtrBase& operator=(GCObject* object)
        {
            _pObject = object;
            return *this;
        }

        bool empty() const
        {
            return _pObject == nullptr;
        }

        void reset()
        {
            _pObject = nullptr;
        }

        GCObject* operator->()
        {
            return _pObject;
        }

        GCObject* get()
        {
            return _pObject;
        }
    };


    template<class T>
    class GCObjectRootPtr : public GCObjectRootPtrBase
    {
    public:
        GCObjectRootPtr(IRootsRegistry& rootsRegistry) : GCObjectRootPtrBase(rootsRegistry)
        {
        }

        GCObjectRootPtr(const GCObjectRootPtr<T>& rhs) : GCObjectRootPtrBase(rhs)
        {
        }

        GCObjectRootPtr& operator=(T* object)
        {
            GCObjectRootPtrBase::operator=(static_cast<GCObject*>(object));

            return *this;
        }

        T* operator->()
        {
            return static_cast<T*>(_pObject);
        }

        T* get()
        {
            return static_cast<T*>(_pObject);
        }
    };
}

