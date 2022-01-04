#include "pch.h"
#include "resource.h"
#include "PBDriver.h"
#include "app.h"
#include "OtherFuncs.h"

CPBApp app;

BOOL CPBApp::InitInstance()
{
    CPBDialog dlg;
    m_pMainWnd = &dlg;

    dlg.DoModal();

    return TRUE;
}

IMPLEMENT_DYNAMIC(CPBDialog, CDialog)

BEGIN_MESSAGE_MAP(CPBDialog, CDialog)
    ON_BN_CLICKED(IDC_ADD_PATH_BTN, OnAddPathBtnClicked)
    ON_MESSAGE(WM_KICKIDLE, OnKickIdle)
    ON_UPDATE_COMMAND_UI(IDC_ADD_PATH_BTN, OnUpdate)
END_MESSAGE_MAP()

void CPBDialog::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_ADD_PATH_EDIT, m_addPathEdit);
    DDX_Control(pDX, IDC_BLOCKED_PATHS_LIST, m_blockedPathsList);
}

BOOL CPBDialog::OnInitDialog()
{
    if (!CDialog::OnInitDialog())
        return FALSE;

    if (!m_pbDriver.IsValid()) {
        MessageBox(L"Can't access Process Blocker device", L"Starting of Process Blocker aborted!", MB_ICONSTOP);
        return FALSE;
    }
    
    RefreshPaths();

    return TRUE;
}

void CPBDialog::OnAddPathBtnClicked() {
    CString path; 
    m_addPathEdit.GetWindowTextW(path);

    if (m_pbDriver.AddPath(path.GetBuffer())) {
        MessageBox(L"Path was successfuly added", MB_OK);
        m_addPathEdit.Clear();
        RefreshPaths();
    }
    else {
        std::wstring errorMsg(L"Path wasn't added. ");
        errorMsg += GetLastErrorStr();
        MessageBox(errorMsg.c_str(), MB_OK);
    }
}

LRESULT CPBDialog::OnKickIdle(WPARAM, LPARAM) {
    UpdateDialogControls(this, FALSE);
    return 0;
}

void CPBDialog::OnUpdate(CCmdUI* pCmdUI) {
    switch (pCmdUI->m_nID) {
    case IDC_ADD_PATH_BTN:
        if (CString path; m_addPathEdit.GetWindowTextW(path), path.GetLength() > 0)
            pCmdUI->Enable(TRUE);
        else
            pCmdUI->Enable(FALSE);
        break;

    default:
        break;
    }
}

void CPBDialog::RefreshPaths() {
    m_blockedPathsList.ResetContent();

    WCHAR* buffer = new (std::nothrow) WCHAR[64];
    
    if (!buffer) {
        MessageBox(L"Not enough memory.", L"Can't refresh paths list.", MB_ICONERROR);
        return;
    }

    DWORD bytesReturned = 0;
    m_pbDriver.GetPaths(buffer, 64, &bytesReturned);

    while (bytesReturned > sizeof(WCHAR)) {
        DWORD pathLength = (DWORD)wcslen(buffer);
        m_blockedPathsList.AddString(buffer);
        buffer += pathLength + 1;
        bytesReturned -= (pathLength + 1) * sizeof(WCHAR);
    }

    delete[] buffer;
}
