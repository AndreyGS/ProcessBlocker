#pragma once

class CPBApp : public CWinApp {
public:
    virtual BOOL InitInstance();
};

class CPBDialog : public CDialog {
public:
    CPBDialog() : CDialog(IDD) {}

protected:
    DECLARE_MESSAGE_MAP()

    virtual BOOL OnInitDialog();
    virtual void DoDataExchange(CDataExchange* pDX);
    
    void RefreshPaths();

    afx_msg void OnAddPathBrowseBtnClicked();

    afx_msg void OnAddPathBtnClicked();
    afx_msg void OnDelPathBtnClicked();

    afx_msg void OnDelAllPathBtnClicked();

    afx_msg void OnSettingsEdit();
    afx_msg void OnSaveSettingsBtnClicked();

    afx_msg LRESULT OnKickIdle(WPARAM, LPARAM);
    afx_msg void OnUpdate(CCmdUI* pCmdUI);
    
    TPBDriver m_pbDriver;

    enum { IDD = IDD_MAIN_DLG };

private:
    DWORD prevMaxPathsSize;

    CEdit m_addPathEdit;
    CListBox m_blockedPathsList;
    CButton m_pbEnableCheck;
    CEdit m_maxPathsSizeEdit;
    CButton m_saveSettingsBtn;
};
