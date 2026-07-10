#ifndef GCOBJECT_ROOT_PTR_H
#define GCOBJECT_ROOT_PTR_H

#include <stdexcept>
#include <type_traits>

#include "IRootsRegistry.h"
#include "GCObject.h"

namespace cppgc
{
    class GCObjectRootPtrBase
    {
        IRootsRegistry* _rootsRegistry;

    protected:
        GCObjectPtr _pObject;

    public:
        explicit GCObjectRootPtrBase(IRootsRegistry& rootsRegistry)
            : _rootsRegistry(&rootsRegistry), _pObject(nullptr)
        {
            _rootsRegistry->addRoot(this);
        }

        virtual ~GCObjectRootPtrBase()
        {
            if (_rootsRegistry)
                _rootsRegistry->removeRoot(this);
        }

        GCObjectRootPtrBase(const GCObjectRootPtrBase& rhs)
            : _rootsRegistry(rhs._rootsRegistry), _pObject(rhs._pObject)
        {
            if (_rootsRegistry)
                _rootsRegistry->addRoot(this);
        }

        GCObjectRootPtrBase& operator=(const GCObjectRootPtrBase& rhs)
        {
            if (this == &rhs)
                return *this;
            if (_rootsRegistry != rhs._rootsRegistry)
                throw std::invalid_argument("cannot assign roots from different collectors");

            _pObject = rhs._pObject;
            return *this;
        }

        GCObjectRootPtrBase& operator=(GCObjectPtr object)
        {
            if (object && (!_rootsRegistry || !_rootsRegistry->owns(object)))
                throw std::invalid_argument("object is not owned by this root's collector");

            _pObject = object;
            return *this;
        }

        bool empty() const noexcept
        {
            return _pObject == nullptr;
        }

        void reset() noexcept
        {
            _pObject = nullptr;
        }

        GCObjectPtr operator->() const noexcept
        {
            return _pObject;
        }

        GCObjectPtr get() const noexcept
        {
            return _pObject;
        }

        void detachRegistry(const IRootsRegistry* rootsRegistry) noexcept
        {
            if (_rootsRegistry == rootsRegistry)
            {
                _rootsRegistry = nullptr;
                _pObject = nullptr;
            }
        }
    };

    template<class T>
    class GCObjectRootPtr : public GCObjectRootPtrBase
    {
        static_assert(std::is_base_of_v<GCObject, T>, "root type must derive from GCObject");

    public:
        explicit GCObjectRootPtr(IRootsRegistry& rootsRegistry)
            : GCObjectRootPtrBase(rootsRegistry)
        {}

        GCObjectRootPtr(const GCObjectRootPtr& rhs)
            : GCObjectRootPtrBase(rhs)
        {}

        GCObjectRootPtr& operator=(const GCObjectRootPtr& rhs)
        {
            GCObjectRootPtrBase::operator=(rhs);
            return *this;
        }

        GCObjectRootPtr& operator=(T* object)
        {
            GCObjectRootPtrBase::operator=(static_cast<GCObjectPtr>(object));
            return *this;
        }

        T* operator->() const noexcept
        {
            return static_cast<T*>(_pObject);
        }

        T* get() const noexcept
        {
            return static_cast<T*>(_pObject);
        }
    };
}

#endif // GCOBJECT_ROOT_PTR_H
