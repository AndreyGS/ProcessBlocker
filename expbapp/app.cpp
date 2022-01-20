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

BEGIN_MESSAGE_MAP(CPBDialog, CDialog)
    ON_BN_CLICKED(IDC_ADD_PATH_BROWSE_BTN, OnAddPathBrowseBtnClicked)
    ON_BN_CLICKED(IDC_ADD_PATH_BTN, OnAddPathBtnClicked)
    ON_BN_CLICKED(IDC_DELETE_PATH_BTN, OnDelPathBtnClicked)
    ON_BN_CLICKED(IDC_DELETE_ALL_PATH_BTN, OnDelAllPathBtnClicked)
    ON_BN_CLICKED(IDC_PROC_BLOCK_ENABLE_CHECK, OnSettingsEdit)
    ON_BN_CLICKED(IDC_SAVE_SETTINGS_BTN, OnSaveSettingsBtnClicked)
    ON_EN_CHANGE(IDC_MAX_PATHS_SIZE_EDIT, OnSettingsEdit)
    ON_MESSAGE(WM_KICKIDLE, OnKickIdle)
    ON_UPDATE_COMMAND_UI(IDC_ADD_PATH_BTN, OnUpdate)
    ON_UPDATE_COMMAND_UI(IDC_DELETE_PATH_BTN, OnUpdate)
    ON_UPDATE_COMMAND_UI(IDC_DELETE_ALL_PATH_BTN, OnUpdate)
END_MESSAGE_MAP()

void CPBDialog::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_ADD_PATH_EDIT, m_addPathEdit);
    DDX_Control(pDX, IDC_BLOCKED_PATHS_LIST, m_blockedPathsList);
    DDX_Control(pDX, IDC_PROC_BLOCK_ENABLE_CHECK, m_pbEnableCheck);
    DDX_Control(pDX, IDC_MAX_PATHS_SIZE_EDIT, m_maxPathsSizeEdit);
    DDX_Control(pDX, IDC_SAVE_SETTINGS_BTN, m_saveSettingsBtn);
}

BOOL CPBDialog::OnInitDialog() {
    if (!CDialog::OnInitDialog())
        return FALSE;

    if (!m_pbDriver.IsValid()) {
        MessageBox(L"Can't access Process Blocker device", L"Starting of Process Blocker aborted!", MB_OK | MB_ICONSTOP);
        return FALSE;
    }
    
    PROC_BLOCK_SETTINGS settings = { 0 };

    if (!m_pbDriver.GetSettings(settings)) {
        MessageBox(L"Can't get Process Blocker settings", L"Starting of Process Blocker aborted!", MB_OK | MB_ICONSTOP);
        return FALSE;
    }

    m_pbEnableCheck.SetCheck(settings.isEnabled);

    prevMaxPathsSize = settings.maxPathsSize;

    m_maxPathsSizeEdit.ModifyStyle(0, ES_NUMBER);
    SetDlgItemInt(IDC_MAX_PATHS_SIZE_EDIT, prevMaxPathsSize, FALSE);

    m_saveSettingsBtn.EnableWindow(FALSE);

    RefreshPaths();

    return TRUE;
}

void CPBDialog::OnAddPathBrowseBtnClicked() {
    OPENFILENAME ofn = { 0 };
    WCHAR szFile[2048] = { 0 };

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Executive\0*.exe\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn) == TRUE)
        m_addPathEdit.SetWindowTextW(ofn.lpstrFile);
}

void CPBDialog::OnAddPathBtnClicked() {
    CString path, pathWithQuotationMarks;
    pathWithQuotationMarks = L"\"";
    m_addPathEdit.GetWindowTextW(path);
    pathWithQuotationMarks += path + L"\"";

    if (m_pbDriver.AddPath(pathWithQuotationMarks.GetBuffer())) {
        MessageBox(L"Path was successfuly added to list", L"", MB_OK | MB_ICONINFORMATION);
        m_addPathEdit.SetWindowTextW(L"");
        RefreshPaths();
    }
    else
        ShowErrorMsg(m_hWnd, L"Path wasn't added to list. ", L"", MB_OK | MB_ICONERROR);
}

void CPBDialog::OnDelPathBtnClicked() {
    if (m_blockedPathsList.GetCurSel() == LB_ERR)
        return;

    CString path;

    m_blockedPathsList.GetText(m_blockedPathsList.GetCurSel(), path);

    if (m_pbDriver.DelPath(path.GetBuffer())) {
        std::wstring msg(L"Path \"");
        msg += path.GetBuffer();
        msg += L"\" was successfuly deleted from list";
        MessageBox(msg.c_str(), L"", MB_OK | MB_ICONINFORMATION);
        m_addPathEdit.SetWindowTextW(L"");
        RefreshPaths();
    }
    else
        ShowErrorMsg(m_hWnd, L"Deleting path from blocked process list ended with error. ", L"", MB_OK | MB_ICONERROR);
}

void CPBDialog::OnDelAllPathBtnClicked() {
    if (MessageBox(L"Delete All Paths from list?", L"", MB_OKCANCEL) == IDOK)
        if (m_pbDriver.DelAllPaths())
            RefreshPaths();
        else
            ShowErrorMsg(m_hWnd, L"Deleting paths from blocked process list ended with error. ", L"", MB_OK | MB_ICONERROR);
}

void CPBDialog::OnSettingsEdit() {
    if (GetDlgItemInt(IDC_MAX_PATHS_SIZE_EDIT) > 1048576) {
        MessageBox(L"Max Paths Size cannot be larger that 1048576!", L"", MB_OK | MB_ICONINFORMATION);
        SetDlgItemInt(IDC_MAX_PATHS_SIZE_EDIT, prevMaxPathsSize, FALSE);
        return;
    }

    prevMaxPathsSize = GetDlgItemInt(IDC_MAX_PATHS_SIZE_EDIT);

    m_saveSettingsBtn.EnableWindow(TRUE);
}

void CPBDialog::OnSaveSettingsBtnClicked() {
    PROC_BLOCK_SETTINGS settings = { bool(m_pbEnableCheck.GetCheck()), GetDlgItemInt(IDC_MAX_PATHS_SIZE_EDIT) };

    if (m_pbDriver.SetSettings(settings)) {
        MessageBox(L"Settings successfully saved!", L"", MB_OK | MB_ICONWARNING);
        m_saveSettingsBtn.EnableWindow(FALSE);
    }
    else
        ShowErrorMsg(m_hWnd, L"Settings not saved! ", L"", MB_OK | MB_ICONERROR);
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

    case IDC_DELETE_PATH_BTN:
        if (m_blockedPathsList.GetCurSel() != LB_ERR)
            pCmdUI->Enable(TRUE);
        else
            pCmdUI->Enable(FALSE);
        break;

    case IDC_DELETE_ALL_PATH_BTN:
        if (m_blockedPathsList.GetCount() > 0)
            pCmdUI->Enable(TRUE);
        else
            pCmdUI->Enable(FALSE);
        break;

    default:
        break;
    }
}

void CPBDialog::RefreshPaths() {
    const DWORD bufferLength = 4096;
    DWORD fromEntry = 0;
    DWORD bytesReturned = 0;
    DWORD lastError = NO_ERROR;
    WCHAR* pBuffer = nullptr, * pTemp = nullptr;

    m_blockedPathsList.ResetContent();

    do {
        pBuffer = new (std::nothrow) WCHAR[bufferLength / sizeof(WCHAR)];

        if (!pBuffer) {
            MessageBox(L"Not enough memory. Can't fill paths list fully.", L"", MB_OK | MB_ICONERROR);
            
            break;
        }

        bytesReturned = 0;

        if (!m_pbDriver.GetPaths(pBuffer, bufferLength, &bytesReturned, fromEntry)) {
            ShowErrorMsg(m_hWnd, L"Reading paths from driver error. ", L"", MB_OK | MB_ICONERROR);

            operator delete[](pBuffer, bufferLength);
            break;
        }

        lastError = GetLastError();

        pTemp = pBuffer;

        while (bytesReturned > sizeof(WCHAR)) {
            DWORD pathLength = (DWORD)wcslen(pTemp);
            m_blockedPathsList.AddString(pTemp);
            pTemp += pathLength + 1;
            bytesReturned -= (pathLength + 1) * sizeof(WCHAR);
        }

        operator delete[](pBuffer, bufferLength);

        fromEntry = m_blockedPathsList.GetCount();

    } while (lastError == ERROR_MORE_DATA);
}
