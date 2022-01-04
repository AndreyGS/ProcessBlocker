#pragma once

class CPBApp : public CWinApp {
public:
    virtual BOOL InitInstance();
};

class CPBDialog : public CDialog {
    DECLARE_DYNAMIC(CPBDialog)

public:
    CPBDialog() : CDialog(IDD) {}

protected:
    DECLARE_MESSAGE_MAP()

    virtual BOOL OnInitDialog();
    virtual void DoDataExchange(CDataExchange* pDX);
    
    void RefreshPaths();

    afx_msg void OnAddPathBtnClicked();

    afx_msg LRESULT OnKickIdle(WPARAM, LPARAM);
    afx_msg void OnUpdate(CCmdUI* pCmdUI);
    
    TPBDriver m_pbDriver;

    enum { IDD = IDD_MAIN_DLG };

public:
    CEdit m_addPathEdit;
    CListBox m_blockedPathsList;
};
