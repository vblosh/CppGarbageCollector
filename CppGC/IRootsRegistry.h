#pragma once
namespace cppgc
{

    class GCObjectRootPtrBase;

    struct IRootsRegistry
    {
        virtual void addRoot(GCObjectRootPtrBase* root) = 0;
        virtual void removeRoot(GCObjectRootPtrBase* root) = 0;

        virtual ~IRootsRegistry() = default;
    };

}
