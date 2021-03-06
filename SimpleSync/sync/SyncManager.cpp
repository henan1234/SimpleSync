#include "stdafx.h"
#include "SyncManager.h"

#include "operations\CopyOperation.h"
#include "operations\RemoveOperation.h"
#include "operations\EmptyOperation.h"
#include "operations\ReplaceOperation.h"
#include "operations\CreateOperation.h"



SyncManager::SyncManager()
    : m_syncDirection(SYNC_DIRECTION::LEFT_TO_RIGHT),
      m_sourceFolder(_T("")),
      m_destinationFolder(_T(""))
{
}

SyncManager::~SyncManager()
{
}



void SyncManager::setSourceFolder(const CString& source)
{
    m_sourceFolder = source;
    clearOperationQueue();
}

void SyncManager::setDestinationFolder(const CString& destination)
{
    m_destinationFolder = destination;
    clearOperationQueue();
}

CString SyncManager::getSourceFolder() const
{
    return m_sourceFolder;
}

CString SyncManager::getDestinationFolder() const
{
    return m_destinationFolder;
}

void SyncManager::setSyncDirection(SYNC_DIRECTION direction)
{
    m_syncDirection = direction;
}



SyncManager::SYNC_DIRECTION SyncManager::getSyncDirection() const
{
    return m_syncDirection;
}

void SyncManager::setOptions(const SyncManagerOptions& options)
{
    m_options = options;
}

SyncManagerOptions SyncManager::getOptions() const
{
    return m_options;
}

void SyncManager::setComparisonParameters(const FileComparisonParameters& params)
{
    m_compareParameters = params;
}

FileComparisonParameters SyncManager::getComparisonParameters() const
{
    return m_compareParameters;
}



BOOL SyncManager::isFileInSourceFolder(const FileProperties& file) const
{
    CString parentFolder = file.getParentFolder();
    return parentFolder.Find(getSourceFolder()) == 0;
}

BOOL SyncManager::isFileInDestinationFolder(const FileProperties& file) const
{
    CString parentFolder = file.getParentFolder();
    return parentFolder.Find(getDestinationFolder()) == 0;
}

BOOL SyncManager::isFileInFileSet(const FileProperties& file,
                                const FileSet &files) const
{
    return files.find(file) != files.end();
}

CString SyncManager::getFileRelativePath(const FileProperties& file,
                                         BOOL withName) const
{
    CString source = file.getRelativePath(getSourceFolder(), withName);
    CString dest = file.getRelativePath(getDestinationFolder(), withName);
    return source.IsEmpty() ? dest : source;
}



BOOL SyncManager::scan(ScanCallback* callback)
{
    clearOperationQueue();

    BOOL sourceExists = folderExists(getSourceFolder());
    BOOL destinationExists = folderExists(getDestinationFolder());

    if (!sourceExists || !destinationExists)
        return FALSE;

    if (getSourceFolder() == getDestinationFolder())
        return FALSE;

    if (getSyncDirection() == SYNC_DIRECTION::RIGHT_TO_LEFT)
        scanFolders(getDestinationFolder(), getSourceFolder(), callback);
    else
        scanFolders(getSourceFolder(), getDestinationFolder(), callback);

    return TRUE;
}

void SyncManager::sync(SyncCallback* callback)
{
    for (SyncOperation::ptr& operation: m_syncOperations)
    {
        if (operation && !operation->isForbidden())
        {
            (*callback)(operation);
            operation->execute();
        }
    }

    clearOperationQueue();
}

SyncManager::OperationQueue SyncManager::getOperationQueue()
{
    return m_syncOperations;
}



BOOL SyncManager::folderExists(const CString& folder) const
{
    DWORD attributes = GetFileAttributes(folder);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL SyncManager::fileMeetsRequirements(const FileProperties& file) const
{
    SyncManagerOptions options = getOptions();
    BOOL result = TRUE;

    if (!options.syncHiddenFiles && file.isHidden())
        result = FALSE;

    if (!options.recursive && file.isFolder())
        result = FALSE;

    return result;
}

SyncManager::FileSet SyncManager::getFilesFromFolder(const CString& folder) const
{
    FileSet files;

    CFileFind fileFinder;
    BOOL hasFiles = fileFinder.FindFile(folder + CString("\\*.*"));

    while (hasFiles)
    {
        hasFiles = fileFinder.FindNextFile();
        
        // Ignore "." and ".."
        if (!fileFinder.IsDots())
        {
            CFileStatus fileProperties;
            CFile::GetStatus(fileFinder.GetFilePath(), fileProperties);

            FileProperties file(fileProperties);

            if (fileMeetsRequirements(file))
                files.insert(file);
        }
    }

    fileFinder.Close();
    return files;
}

void SyncManager::scanFolders(const CString& source,
                              const CString& destination,
                              ScanCallback* callback)
{
    // TODO: pass relative, not absolute path
    (*callback)(source);

    FileSet sourceFiles = getFilesFromFolder(source);
    FileSet destinationFiles = getFilesFromFolder(destination);

    for (auto fileIt = sourceFiles.cbegin(); fileIt != sourceFiles.cend(); )
    {
        const FileProperties& file = *fileIt;

        if (!isFileInFileSet(file, destinationFiles))
            manageCopyOperation(file, destination);
        else
        {
            auto sameFileIt = destinationFiles.find(file);
            const FileProperties& sameFile = *sameFileIt;
            
            if (file.isFolder())
            {
                enqueueOperation(new EmptyOperation(file, sameFile));
                scanFolders(file.getFullPath(), sameFile.getFullPath(), callback);
            }
            else
                manageReplaceOperation(file, sameFile);

            destinationFiles.erase(sameFileIt);
        }

        fileIt = sourceFiles.erase(fileIt);
    }


    for (auto fileIt = destinationFiles.cbegin(); fileIt != destinationFiles.cend();)
    {
        const FileProperties& file = *fileIt;

        if (!isFileInFileSet(file, sourceFiles))
        {
            if (getSyncDirection() == SYNC_DIRECTION::BOTH)
                manageCopyOperation(file, source);
            else
                manageRemoveOperation(file);
        }

        fileIt = destinationFiles.erase(fileIt);
    }
}



void SyncManager::enqueueOperation(SyncOperation* operation)
{
    if (operation)
        m_syncOperations.push_back(SyncOperation::ptr(operation));
}

void SyncManager::clearOperationQueue()
{
    m_syncOperations.clear();
}

void SyncManager::manageCopyOperation(const FileProperties& fileToCopy,
                                      const CString& destinationFolder)
{
    if (fileToCopy.isFolder())
    {
        BOOL isEmpty = PathIsDirectoryEmpty(fileToCopy.getFullPath());
        if (isEmpty && !getOptions().createEmptyFolders)
            return;

        CString folderToCreate = destinationFolder + "\\" + fileToCopy.getFileName();
        enqueueOperation(new CreateFolderOperation(fileToCopy, folderToCreate));

        FileSet files = getFilesFromFolder(fileToCopy.getFullPath());

        // Recursively copy files and subfolders
        for (const auto& file : files)
            manageCopyOperation(file, folderToCreate);
    }
    else
    {
        if (getOptions().copyMissingFiles)
            enqueueOperation(new CopyOperation(fileToCopy, destinationFolder));
    }
}

void SyncManager::manageReplaceOperation(const FileProperties& originalFile,
                                         const FileProperties& fileToReplace)
{
    using RESULT = FileProperties::COMPARISON_RESULT;

    RESULT compareResult = originalFile.compareTo(fileToReplace,
                                                  getComparisonParameters());
    SyncOperation* op = NULL;

    // Find out ambiguity and direction
    switch (compareResult)
    {
    case RESULT::PREFERABLE:
        op = new ReplaceOperation(originalFile, fileToReplace, FALSE);
        break;
    case RESULT::NON_PREFERABLE:
        if (getSyncDirection() == SYNC_DIRECTION::BOTH)
            op = new ReplaceOperation(fileToReplace, originalFile, FALSE);
        else
            op = new ReplaceOperation(originalFile, fileToReplace, TRUE);
        break;
    case RESULT::UNDEFINED:
        op = new ReplaceOperation(originalFile, fileToReplace, TRUE);
        break;
    case RESULT::EQUAL:
        op = new EmptyOperation(originalFile, fileToReplace);
        break;
    }
    enqueueOperation(op);
}

void SyncManager::manageRemoveOperation(const FileProperties& fileToRemove)
{
    if (fileToRemove.isFolder())
    {
        FileSet files = getFilesFromFolder(fileToRemove.getFullPath());

        // Recursively remove files and subfolders
        for (const auto& file : files)
            manageRemoveOperation(file);
    }

    if (getOptions().deleteFiles)
        enqueueOperation(new RemoveOperation(fileToRemove));
}
