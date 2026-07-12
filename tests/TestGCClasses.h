#pragma once

#include "GCObject.h"

class HeaderDefinedNode : public cppgc::GCObject
{
    DECLARE_GCOBJECT_CLASS(HeaderDefinedNode)

public:
    HeaderDefinedNode() : next(this)
    {}

    cppgc::GCMember<HeaderDefinedNode> next;
};

GCOBJECT_POINTER_MAP_BEGIN(HeaderDefinedNode)
GCPOINTER(HeaderDefinedNode, next)
GCOBJECT_POINTER_MAP_END(HeaderDefinedNode)

const cppgc::ClassInfo* getHeaderDefinedNodeInfoFromOtherTranslationUnit();
