#ifndef SOURCE_DROP_TARGET_H
#define SOURCE_DROP_TARGET_H

#include <windows.h>
#include <oleidl.h>
#include <string>
#include <vector>
#include <functional>

// true means a drop is currently allowed false means show the no drop cursor
// extracted as a pure function so it has a dedicated print flag test
// separate from the com plumbing around it
bool ShouldAllowSourceDrop(bool serverBusyOrRunning);

// minimal ole drop target for the media source listbox
// accepts an hdrop only rejects everything else and rejects all drops
// while the server is starting running or stopping
class SourceListDropTarget : public IDropTarget {
public:
    // onFilesDropped receives the list of dropped file system paths in
    // the order explorer reported them folders and files both included
    // isServerBusyOrRunning is polled fresh on every drag enter drag over
    // and drop call so the cursor always reflects the current state
    SourceListDropTarget(std::function<bool()> isServerBusyOrRunning,
                        std::function<void(const std::vector<std::wstring>&)> onFilesDropped);

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG __stdcall AddRef() override;
    ULONG __stdcall Release() override;

    // IDropTarget
    HRESULT __stdcall DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    HRESULT __stdcall DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;
    HRESULT __stdcall DragLeave() override;
    HRESULT __stdcall Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override;

private:
    void ComputeEffect(DWORD* pdwEffect) const;

    LONG m_refCount;
    std::function<bool()> m_isServerBusyOrRunning;
    std::function<void(const std::vector<std::wstring>&)> m_onFilesDropped;
};

#endif // SOURCE_DROP_TARGET_H
