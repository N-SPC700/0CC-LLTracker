/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** 0CC-FamiTracker is (C) 2014-2018 HertzDevil
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/


#pragma once

#include "stdafx.h"		// // //
#include "../resource.h"		// // //
#include "SoundChipSet.h"		// // //

class CFamiTrackerDoc;
class CFamiTrackerModule;		// // //

// CModulePropertiesDlg dialog

class CModulePropertiesDlg : public CDialog
{
	DECLARE_DYNAMIC(CModulePropertiesDlg)

private:
	void SelectSong(int Song);
	void UpdateSongButtons();

	bool m_bSingleSelection;		// // //
	unsigned int m_iSelectedSong;
	CSoundChipSet m_iExpansions;		// // //
	int m_iN163Channels;

	CFamiTrackerDoc *m_pDocument = nullptr;
	CFamiTrackerModule *m_pModule = nullptr;		// // //

public:
	CModulePropertiesDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~CModulePropertiesDlg();

// Dialog Data
	enum { IDD = IDD_PROPERTIES };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	CStringW GetSongString(unsigned index) const;		// // //
	void FillSongList();

	// // //
	CListCtrl m_cListSongs;

	/*CButton m_cButtonEnableVRC6;
	CButton m_cButtonEnableVRC7;
	CButton m_cButtonEnableFDS;
	CButton m_cButtonEnableMMC5;
	CButton m_cButtonEnableN163;
	CButton m_cButtonEnableS5B;
	CSliderCtrl m_cSliderN163Chans;
	CStatic m_cStaticN163Chans;*/

	CComboBox m_cComboVibrato;
	CComboBox m_cComboLinearPitch;

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedSongAdd();
	afx_msg void OnBnClickedSongInsert();		// // //
	afx_msg void OnBnClickedSongRemove();
	afx_msg void OnBnClickedSongUp();
	afx_msg void OnBnClickedSongDown();
	afx_msg void OnEnChangeSongname();
	afx_msg void OnBnClickedSongImport();
	// afx_msg void OnCbnSelchangeExpansion();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnLvnItemchangedSonglist(NMHDR *pNMHDR, LRESULT *pResult);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	/*afx_msg void OnBnClickedExpansionVRC6();		// // //
	afx_msg void OnBnClickedExpansionVRC7();
	afx_msg void OnBnClickedExpansionFDS();
	afx_msg void OnBnClickedExpansionMMC5();
	afx_msg void OnBnClickedExpansionS5B();
	afx_msg void OnBnClickedExpansionN163();*/
	afx_msg void OnCbnSelchangeComboLinearpitch();
};
