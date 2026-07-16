#include "source_drop_target.h"
#include <shellapi.h>
#include <string>

bool ShouldAllowSourceDrop(bool serverBusyOrRunning) {
    return !serverBusyOrRunning;
}

SourceListDropTarget::SourceListDropTarget(std::function<bool()> isServerBusyOrRunning,
                                           std::function<void(const std::vector<std::wstring>&)> onFilesDropped)
    : m_refCount(1),
      m_isServerBusyOrRunning(std::move(isServerBusyOrRunning)),
      m_onFilesDropped(std::move(onFilesDropped)) {
}

HRESULT __stdcall SourceListDropTarget::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppvObject = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG __stdcall SourceListDropTarget::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

ULONG __stdcall SourceListDropTarget::Release() {
    LONG remaining = InterlockedDecrement(&m_refCount);
    if (remaining == 0) {
        delete this;
    }
    return static_cast<ULONG>(remaining);
}

void SourceListDropTarget::ComputeEffect(DWORD* pdwEffect) const {
    bool busyOrRunning = m_isServerBusyOrRunning ? m_isServerBusyOrRunning() : false;
    *pdwEffect = ShouldAllowSourceDrop(busyOrRunning) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

HRESULT __stdcall SourceListDropTarget::DragEnter(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) {
    FORMATETC formatEtc{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    HRESULT supported = pDataObj ? pDataObj->QueryGetData(&formatEtc) : E_INVALIDARG;
    if (FAILED(supported)) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
    ComputeEffect(pdwEffect);
    return S_OK;
}

HRESULT __stdcall SourceListDropTarget::DragOver(DWORD, POINTL, DWORD* pdwEffect) {
    ComputeEffect(pdwEffect);
    return S_OK;
}

HRESULT __stdcall SourceListDropTarget::DragLeave() {
    return S_OK;
}

HRESULT __stdcall SourceListDropTarget::Drop(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) {
    ComputeEffect(pdwEffect);
    if (*pdwEffect == DROPEFFECT_NONE || !pDataObj) {
        return S_OK;
    }

    FORMATETC formatEtc{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium{};
    if (FAILED(pDataObj->GetData(&formatEtc, &medium))) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    HDROP hDrop = static_cast<HDROP>(medium.hGlobal);
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    paths.reserve(fileCount);
    for (UINT i = 0; i < fileCount; ++i) {
        UINT length = DragQueryFileW(hDrop, i, nullptr, 0);
        std::wstring path(length, L'\0');
        DragQueryFileW(hDrop, i, &path[0], length + 1);
        path.resize(length);
        paths.push_back(path);
    }
    ReleaseStgMedium(&medium);

    if (m_onFilesDropped && !paths.empty()) {
        m_onFilesDropped(paths);
    }
    return S_OK;
}
