#ifndef IROOTS_REGISTRY_H
#define IROOTS_REGISTRY_H

namespace cppgc
{
    class GCObject;
    class GCObjectRootPtrBase;
    class GCObjectWeakPtrBase;

    struct IRootsRegistry
    {
        virtual void addRoot(GCObjectRootPtrBase* root) = 0;
        virtual void removeRoot(GCObjectRootPtrBase* root) = 0;
        virtual void addWeak(GCObjectWeakPtrBase* weak) = 0;
        virtual void removeWeak(GCObjectWeakPtrBase* weak) = 0;
        virtual bool owns(const GCObject* object) const = 0;

        virtual ~IRootsRegistry() = default;
    };

}

#endif // IROOTS_REGISTRY_H
