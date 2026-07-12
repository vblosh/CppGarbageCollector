#include "TestGCClasses.h"

const cppgc::ClassInfo* getHeaderDefinedNodeInfoFromOtherTranslationUnit()
{
    return HeaderDefinedNode::GetClassInfo();
}
