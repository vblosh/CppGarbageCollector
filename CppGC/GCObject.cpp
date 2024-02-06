#include "GCObject.h"
namespace cppgc
{

	bool isSubclassOf(GCObject* descendant, GCObject* ancestor)
	{
		ClassInfo* currentInfo = descendant->getClassInfo();
		ClassInfo* ancestorInfo = ancestor->getClassInfo();
		do {
			if (currentInfo == ancestorInfo)
				return true;
			else
				currentInfo = currentInfo->parentInfo;
		} while (currentInfo);
		return false;
	}

}