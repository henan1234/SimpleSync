#include "stdafx.h"
#include "RemoveOperation.h"



RemoveOperation::RemoveOperation(const FileProperties& fileToDelete)
    : SyncOperation(SyncOperation::TYPE::REMOVE, fileToDelete)
{
}


RemoveOperation::~RemoveOperation()
{
}

BOOL RemoveOperation::execute()
{
    if (getFile().isDirectory())
        return RemoveDirectory(getFile().getFullPath());
    else
        return DeleteFile(getFile().getFullPath());
}

BOOL RemoveOperation::isOperationDependent(const SyncOperation* dependentOp) const
{
    if (!getFile().isDirectory())
        return FALSE;
    else
        return dependentOp->getFile().isParentFolder(getFile());
}
