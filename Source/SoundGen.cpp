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

//
// This file takes care of the NES sound playback
//
// TODO:
//  - Create new interface for CFamiTrackerView with thread-safe functions
//  - Same for CFamiTrackerDoc
//  - Perhaps this should be a worker thread and not GUI thread?
//

/*/ // //
CSoundGen depends on CFamiTrackerView for:
 - MakeSilent
 - GetSelectedPos
 - GetSelectedChannelID
 - PlayerPlayNote
// // /*/

#include "SoundGen.h"
#include "SongState.h"		// // //
#include "FamiTrackerEnv.h"		// // //
#include "FamiTrackerDoc.h"
#include "FamiTrackerModule.h"		// // //
#include "FamiTrackerView.h"
#include "VisualizerWnd.h"
#include "MainFrm.h"
#include "DirectSound.h"
#include "APU/APU.h"
#include "APU/2A03.h"		// // //
#include "APU/Mixer.h"		// // // CHIP_LEVEL_*
#include "SoundChipSet.h"		// // //
#include "ft0cc/doc/dpcm_sample.hpp"		// // //
#include "InstrumentRecorder.h"		// // //
#include "Settings.h"
#include "MIDI.h"
#include "SongData.h"		// // //
#include "Arpeggiator.h"		// // //
#include "TempoCounter.h"		// // //
#include "TempoDisplay.h"		// // // 050B
#include "AudioDriver.h"		// // //
#include "WaveRenderer.h"		// // //
#include "SoundDriver.h"		// // //
#include "PatternNote.h"		// // //
#include "ChannelMap.h"		// // //
#include "TrackerChannel.h"		// // //
#include "Highlight.h"		// // //
#include "Bookmark.h"		// // //
#include "Instrument.h"
#include "str_conv/str_conv.hpp"		// // //

// // // Log VGM output (port from sn7t when necessary)
//#define WRITE_VGM



namespace {

const std::size_t DEFAULT_AVERAGE_BPM_SIZE = 24;

} // namespace

IMPLEMENT_DYNCREATE(CSoundGen, CWinThread)

BEGIN_MESSAGE_MAP(CSoundGen, CWinThread)
	ON_THREAD_MESSAGE(WM_USER_SILENT_ALL, OnSilentAll)
	ON_THREAD_MESSAGE(WM_USER_LOAD_SETTINGS, OnLoadSettings)
	ON_THREAD_MESSAGE(WM_USER_PLAY, OnStartPlayer)
	ON_THREAD_MESSAGE(WM_USER_STOP, OnStopPlayer)
	ON_THREAD_MESSAGE(WM_USER_RESET, OnResetPlayer)
	ON_THREAD_MESSAGE(WM_USER_START_RENDER, OnStartRender)
	ON_THREAD_MESSAGE(WM_USER_STOP_RENDER, OnStopRender)
	ON_THREAD_MESSAGE(WM_USER_PREVIEW_SAMPLE, OnPreviewSample)
	ON_THREAD_MESSAGE(WM_USER_WRITE_APU, OnWriteAPU)
	ON_THREAD_MESSAGE(WM_USER_CLOSE_SOUND, OnCloseSound)
	ON_THREAD_MESSAGE(WM_USER_SET_CHIP, OnSetChip)
	ON_THREAD_MESSAGE(WM_USER_REMOVE_DOCUMENT, OnRemoveDocument)
END_MESSAGE_MAP()



// CSoundGen

CSoundGen::CSoundGen() :
	m_pTempoCounter(std::make_shared<CTempoCounter>()),		// // //
	m_pSoundDriver(std::make_unique<CSoundDriver>(this)),		// // //
	m_pAPU(std::make_unique<CAPU>()),		// // //
	m_bHaltRequest(false),
	m_pInstRecorder(std::make_unique<CInstrumentRecorder>(this)),		// // //
	m_bWaveChanged(0),
	m_iMachineType(machine_t::NTSC),
	m_bRunning(false),
	m_hInterruptEvent(NULL),
	m_pArpeggiator(std::make_unique<CArpeggiator>()),		// // //
	m_pSequencePlayPos(NULL),
	m_iSequencePlayPos(0),
	m_iSequenceTimeout(0)
{
	TRACE(L"SoundGen: Object created\n");

	// Create all kinds of channels
	m_pSoundDriver->SetupTracks();		// // //
}

CSoundGen::~CSoundGen()
{
}

//
// Object initialization, global
//

void CSoundGen::AssignDocument(CFamiTrackerDoc *pDoc)
{
	// Called from main thread
	ASSERT(GetCurrentThreadId() == FTEnv.GetMainApp()->m_nThreadID);

	// Ignore all but the first document (as new documents are used to import files)
	if (m_pDocument)
		return;

	// Assigns a document to this object
	m_pDocument = pDoc;
	AssignModule(*m_pDocument->GetModule());		// // //

	m_pSoundDriver->LoadAPU(*m_pAPU);		// // //
	m_pSoundDriver->SetTempoCounter(m_pTempoCounter);		// // //

	DocumentPropertiesChanged(pDoc);		// // //
}

void CSoundGen::AssignModule(CFamiTrackerModule &modfile) {
	m_pModule = &modfile;
	m_pInstRecorder->AssignModule(modfile);
	m_pSoundDriver->AssignModule(modfile);
	m_pTempoCounter->AssignModule(modfile);
}

void CSoundGen::AssignView(CFamiTrackerView *pView)
{
	// Called from main thread
	ASSERT(GetCurrentThreadId() == FTEnv.GetMainApp()->m_nThreadID);

	if (m_pTrackerView != NULL)
		return;

	// Assigns the tracker view to this object
	m_pTrackerView = pView;
}

void CSoundGen::RemoveDocument()
{
	// Removes both the document and view from this object

	// Called from main thread
	ASSERT(GetCurrentThreadId() == FTEnv.GetMainApp()->m_nThreadID);
	ASSERT(m_pDocument != NULL);
	ASSERT(m_hThread != NULL);

	// Player cannot play when removing the document
	StopPlayer();
	WaitForStop();

	PostThreadMessageW(WM_USER_REMOVE_DOCUMENT, 0, 0);

	// Wait 5s for thread to clear the pointer
	for (int i = 0; i < 50 && m_pDocument != NULL; ++i)
		Sleep(100);

	if (m_pDocument != NULL) {
		// Thread stuck
		TRACE(L"SoundGen: Could not remove document pointer!\n");
	}
}

void CSoundGen::SetVisualizerWindow(CVisualizerWnd *pWnd)
{
	// Called from main thread
	ASSERT(GetCurrentThreadId() == FTEnv.GetMainApp()->m_nThreadID);

	m_csVisualizerWndLock.Lock();
	m_pVisualizerWnd = pWnd;
	m_csVisualizerWndLock.Unlock();
}

void CSoundGen::ModuleChipChanged() {		// // //
	// Tell the sound emulator to switch expansion chip
	SelectChip(m_pModule ? m_pModule->GetSoundChipSet() : sound_chip_t::APU);

	// Change period tables
	if (m_pModule)
		LoadMachineSettings();
}

void CSoundGen::SelectChip(CSoundChipSet Chip)
{
	if (IsPlaying()) {
		StopPlayer();
	}

	if (!WaitForStop()) {
		TRACE(L"CSoundGen: Could not stop player!");
		return;
	}

	PostThreadMessageW(WM_USER_SET_CHIP, Chip.GetFlag(), 0);
}

void CSoundGen::DocumentPropertiesChanged(CFamiTrackerDoc *pDocument)
{
//	ASSERT(pDocument == m_pDocument);		// // //
	if (pDocument != m_pDocument)
		return;
	AssignModule(*m_pDocument->GetModule());
	m_pSoundDriver->ConfigureDocument();
}

//
// Interface functions
//

void CSoundGen::StartPlayer(std::unique_ptr<CPlayerCursor> Pos)		// // //
{
	if (!m_hThread)
		return;

	PostThreadMessageW(WM_USER_PLAY, reinterpret_cast<uintptr_t>(Pos.release()), 0);
}

void CSoundGen::StopPlayer()
{
	if (!m_hThread)
		return;

	PostThreadMessageW(WM_USER_STOP, 0, 0);

	FTEnv.GetMIDI()->ResetOutput();		// // //
}

void CSoundGen::ResetPlayer(int Track)
{
	if (!m_hThread)
		return;

	auto pCur = std::make_unique<CPlayerCursor>(*m_pModule->GetSong(Track), Track);		// // //
	PostThreadMessageW(WM_USER_RESET, reinterpret_cast<uintptr_t>(pCur.release()), 0);
}

void CSoundGen::LoadSettings()
{
	if (!m_hThread)
		return;

	PostThreadMessageW(WM_USER_LOAD_SETTINGS, 0, 0);
}

void CSoundGen::SilentAll()
{
	if (!m_hThread)
		return;

	PostThreadMessageW(WM_USER_SILENT_ALL, 0, 0);
}

void CSoundGen::PlaySingleRow(int track) {		// // //
	m_iLastTrack = track;

	if (!m_bPlayingSingleRow) {
		ApplyGlobalState();
		m_bPlayingSingleRow = true;
	}

	auto [frame, row] = m_pTrackerView->GetSelectedPos();
	const CSongData &song = *m_pModule->GetSong(track);
	m_pModule->GetChannelOrder().ForeachChannel([&] (stChannelID i) {
		if (!IsChannelMuted(i))
			QueueNote(i, song.GetActiveNote(i, frame, row), NOTE_PRIO_1);
	});
}

void CSoundGen::WriteAPU(int Address, char Value)
{
	if (!m_hThread)
		return;

	// Direct APU interface
	PostThreadMessageW(WM_USER_WRITE_APU, (WPARAM)Address, (LPARAM)Value);
}

bool CSoundGen::IsExpansionEnabled(sound_chip_t Chip) const {		// // //
	return m_pModule && m_pModule->HasExpansionChip(Chip);
}

int CSoundGen::GetNamcoChannelCount() const {		// // //
	return m_pModule ? m_pModule->GetNamcoChannels() : 0;
}

void CSoundGen::PreviewSample(std::shared_ptr<const ft0cc::doc::dpcm_sample> pSample, int Offset, int Pitch)		// // //
{
	if (!m_hThread)
		return;

	m_pPreviewSample = std::move(pSample);
	// Preview a DPCM sample. If the name of sample is null,
	// the sample will be removed after played
	PostThreadMessageW(WM_USER_PREVIEW_SAMPLE, Offset, Pitch);
}

void CSoundGen::CancelPreviewSample()
{
	// Remove references to selected sample.
	// This must be done if a sample is about to be deleted!
	if (auto *p2A03 = dynamic_cast<C2A03 *>(m_pAPU->GetSoundChip(sound_chip_t::APU)))		// // //
		p2A03->ClearSample();
}

bool CSoundGen::IsRunning() const
{
	return (m_hThread != NULL) && m_bRunning;
}

bool CSoundGen::Shutdown() {		// // //
	// Thread was not suspended, send quit message
	if (ResumeThread() == 0)
		PostThreadMessageW(WM_QUIT, 0, 0);

	// If thread was suspended then it will auto-terminate, because sound hasn't been initialized

	// Wait for thread to exit
	DWORD dwResult = ::WaitForSingleObject(m_hThread, 3000); // calls CFamiTrackerApp::DetachSoundGenerator
	return dwResult == WAIT_OBJECT_0;
}

//// Sound buffer handling /////////////////////////////////////////////////////////////////////////////////

bool CSoundGen::InitializeSound(HWND hWnd)
{
	// Initialize sound, this is only called once!
	// Start with NTSC by default

	// Called from main thread
	ASSERT(GetCurrentThread() == FTEnv.GetMainApp()->m_hThread);
	ASSERT(!m_pDSound);

	m_bAutoDelete = FALSE;		// // //

	// Event used to interrupt the sound buffer synchronization
	m_hInterruptEvent = ::CreateEventW(NULL, FALSE, FALSE, NULL);

	// Create DirectSound object
	m_pDSound = std::make_unique<CDSound>(hWnd, m_hInterruptEvent);		// // //

	// Out of memory
	if (!m_pDSound)
		return false;

	m_pDSound->EnumerateDevices();

	// Start thread when audio is done
	ResumeThread();

	return true;
}

void CSoundGen::Interrupt() const
{
	if (m_hInterruptEvent != NULL)
		::SetEvent(m_hInterruptEvent);
}

bool CSoundGen::ResetAudioDevice()
{
	// Setup sound, return false if failed
	//
	// The application must be able to continue even if this fails
	//

	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);
	ASSERT(m_pDSound != NULL);

	CSettings *pSettings = FTEnv.GetSettings();

	unsigned int SampleSize = pSettings->Sound.iSampleSize;
	unsigned int SampleRate = pSettings->Sound.iSampleRate;
	unsigned int BufferLen	= pSettings->Sound.iBufferLength;
	unsigned int Device		= pSettings->Sound.iDevice;

	if (m_pAudioDriver)
		m_pAudioDriver->CloseAudioDevice();		// // //

	if (Device >= m_pDSound->GetDeviceCount()) {
		// Invalid device detected, reset to 0
		Device = 0;
		pSettings->Sound.iDevice = 0;
	}

	// Reinitialize direct sound
	if (!m_pDSound->SetupDevice(Device)) {
		AfxMessageBox(IDS_DSOUND_ERROR, MB_ICONERROR);
		return false;
	}

	int iBlocks = 2;	// default = 2

	// Create more blocks if a bigger buffer than 100ms is used to reduce lag
	if (BufferLen > 100)
		iBlocks += (BufferLen / 66);

	m_pAudioDriver = std::make_unique<CAudioDriver>(*this,
		std::move(m_pDSound->OpenChannel(SampleRate, SampleSize, 1, BufferLen, iBlocks)), SampleSize);

	// Channel failed
	if (!m_pAudioDriver || !m_pAudioDriver->IsAudioDeviceOpen()) {
		AfxMessageBox(IDS_DSOUND_BUFFER_ERROR, MB_ICONERROR);
		return false;
	}

	// Sample graph rate
	m_csVisualizerWndLock.Lock();
	if (m_pVisualizerWnd)
		m_pVisualizerWnd->SetSampleRate(SampleRate);
	m_csVisualizerWndLock.Unlock();

	m_pAPU->SetCallback(*m_pAudioDriver);
	if (!m_pAPU->SetupSound(SampleRate, 1, m_iMachineType))		// // //
		return false;

	m_pAPU->SetChipLevel(CHIP_LEVEL_APU1, float(pSettings->ChipLevels.iLevelAPU1 / 10.0f));
	m_pAPU->SetChipLevel(CHIP_LEVEL_APU2, float(pSettings->ChipLevels.iLevelAPU2 / 10.0f));
	m_pAPU->SetChipLevel(CHIP_LEVEL_VRC6, float(pSettings->ChipLevels.iLevelVRC6 / 10.0f));
	m_pAPU->SetChipLevel(CHIP_LEVEL_VRC7, float(pSettings->ChipLevels.iLevelVRC7 / 10.0f));
	m_pAPU->SetChipLevel(CHIP_LEVEL_MMC5, float(pSettings->ChipLevels.iLevelMMC5 / 10.0f));
	m_pAPU->SetChipLevel(CHIP_LEVEL_FDS, float(pSettings->ChipLevels.iLevelFDS / 10.0f));
	m_pAPU->SetChipLevel(CHIP_LEVEL_N163, float(pSettings->ChipLevels.iLevelN163 / 10.0f));
	m_pAPU->SetChipLevel(CHIP_LEVEL_S5B, float(pSettings->ChipLevels.iLevelS5B / 10.0f));

	// Update blip-buffer filtering
	m_pAPU->SetupMixer(pSettings->Sound.iBassFilter, pSettings->Sound.iTrebleFilter,
					   pSettings->Sound.iTrebleDamping, pSettings->Sound.iMixVolume);

	TRACE(L"SoundGen: Created sound channel with params: %i Hz, %i bits, %i ms (%i blocks)\n", SampleRate, SampleSize, BufferLen, iBlocks);

	return true;
}

void CSoundGen::CloseAudio()
{
	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);

	if (m_pAudioDriver) {		// // //
		m_pAudioDriver->CloseAudioDevice();
		m_pAudioDriver.reset();
	}

	if (m_pDSound) {
		m_pDSound->CloseDevice();
		m_pDSound.reset();		// // //
	}

	if (m_hInterruptEvent) {
		::CloseHandle(m_hInterruptEvent);
		m_hInterruptEvent = NULL;
	}
}

bool CSoundGen::IsAudioReady() const {		// // //
	return m_pDocument && m_pModule && m_pAudioDriver->IsAudioDeviceOpen() && m_pDocument->IsFileLoaded();
}

void CSoundGen::ResetBuffer()
{
	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);

	m_pAudioDriver->Reset();		// // //
	m_pAPU->Reset();
}

void CSoundGen::FlushBuffer(array_view<int16_t> Buffer)		// // //
{
	// Callback method from emulation

	// May only be called from sound player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);

	m_pAudioDriver->FlushBuffer(Buffer);		// // //
}

// // //
CDSound *CSoundGen::GetSoundInterface() const {
	return m_pDSound.get();
}

CAudioDriver *CSoundGen::GetAudioDriver() const {
	return m_pAudioDriver.get();
}

bool CSoundGen::PlayBuffer()
{
	ASSERT(GetCurrentThreadId() == m_nThreadID);

	if (m_pWaveRenderer) {		// // //
		CSingleLock l(&m_csRenderer); l.Lock();
		if (is_rendering_impl()) {
			auto buf = m_pAudioDriver->ReleaseSoundBuffer();
			switch (m_pAudioDriver->GetSampleSize()) {
			case 8:
				if (m_bRenderingWave) m_pWaveRenderer->FlushBuffer(array_view<std::uint8_t>(
					reinterpret_cast<const std::uint8_t *>(buf.data()), buf.size() / sizeof(std::uint8_t)));		// // //
				return true;
			case 16:
				if (m_bRenderingWave) m_pWaveRenderer->FlushBuffer(array_view<std::int16_t>(
					reinterpret_cast<const std::int16_t *>(buf.data()), buf.size() / sizeof(std::int16_t)));		// // //
				return true;
			}
			return false;
		}
	}

	if (!m_pAudioDriver->DoPlayBuffer())
		return false;

	// // // Draw graph
//	if (!IsBackgroundTask()) {
	if (!is_rendering_impl()) {
		m_csVisualizerWndLock.Lock();
		if (m_pVisualizerWnd)
			m_pVisualizerWnd->FlushSamples(m_pAudioDriver->ReleaseGraphBuffer());
		m_csVisualizerWndLock.Unlock();
	}

	return true;
}

unsigned int CSoundGen::GetFrameRate()
{
	return std::exchange(m_iFrameCounter, 0);		// // //
}

//// Tracker playing routines //////////////////////////////////////////////////////////////////////////////

int CSoundGen::ReadPeriodTable(int Index, int Table) const		// // //
{
	return m_pSoundDriver->ReadPeriodTable(Index, Table);		// // //
}

void CSoundGen::BeginPlayer(std::unique_ptr<CPlayerCursor> Pos)		// // //
{
	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);
//	ASSERT(m_pTrackerView != NULL);

	if (!IsAudioReady())		// // //
		return;

	auto &cur = *Pos;		// // //
	m_pSoundDriver->StartPlayer(std::move(Pos));		// // //

	m_bHaltRequest		= false;
	m_bPlayingSingleRow = false;		// // //
	m_iLastTrack		= cur.GetCurrentSong();		// // //

#ifdef WRITE_VGM		// // //
	m_pVGMWriter = std::make_unique<CVGMWriter>();
#endif

	if (FTEnv.GetSettings()->Display.bAverageBPM)		// // // 050B
		m_pTempoDisplay = std::make_unique<CTempoDisplay>(*m_pTempoCounter, DEFAULT_AVERAGE_BPM_SIZE);

	ResetTempo();
	ResetAPU();

	MakeSilent();

	if (FTEnv.GetSettings()->General.bRetrieveChanState)		// // //
		ApplyGlobalState();

	if (m_pInstRecorder->GetRecordChannel().Chip != sound_chip_t::none)		// // //
		m_pInstRecorder->StartRecording();
}

void CSoundGen::ApplyGlobalState()		// // //
{
	CSingleLock l(&m_csAPULock, TRUE);		// // //
	auto [Frame, Row] = IsPlaying() ? GetPlayerPos() : m_pTrackerView->GetSelectedPos();		// // //

	CSongState state;
	state.Retrieve(*m_pModule, GetPlayerTrack(), Frame, Row);

	m_pSoundDriver->LoadSoundState(state);

	m_iLastHighlight = m_pModule->GetSong(GetPlayerTrack())->GetHighlightAt(Frame, Row).First;
}

// // //
void CSoundGen::OnTick() {
	if (m_pTempoDisplay)		// // // 050B
		m_pTempoDisplay->Tick();
	CSingleLock l(&m_csRenderer); l.Lock();
	if (is_rendering_impl())
		m_pWaveRenderer->Tick();
}

void CSoundGen::OnStepRow() {
	if (m_pTempoDisplay)		// // // 050B
		m_pTempoDisplay->StepRow();
	CSingleLock l(&m_csRenderer); l.Lock();
	if (is_rendering_impl())
		m_pWaveRenderer->StepRow();		// // //
}

void CSoundGen::OnPlayNote(stChannelID chan, const stChanNote &note) {
	if (!IsChannelMuted(chan)) {
		if (m_pTrackerView)
			m_pTrackerView->PlayerPlayNote(chan, note);
		FTEnv.GetMIDI()->WriteNote((uint8_t)m_pModule->GetChannelOrder().GetChannelIndex(chan), note.Note, note.Octave, note.Vol);
	}
}

void CSoundGen::OnUpdateRow(int frame, int row) {
	auto *pMark = m_pModule->GetSong(m_iLastTrack)->GetBookmarks().FindAt(frame, row);
	if (pMark && pMark->m_Highlight.First != -1)		// // //
		m_iLastHighlight = pMark->m_Highlight.First;
	if (!IsBackgroundTask() && m_pTrackerView)		// // //
		m_pTrackerView->PostMessageW(WM_USER_PLAYER, frame, row);
}

void CSoundGen::SetChannelMute(stChannelID chan, bool mute) {		// // //
	muted_[chan] = mute;
	if (mute && chan == GetRecordChannel())
		SetRecordChannel({ });
}

bool CSoundGen::IsChannelMuted(stChannelID chan) const {		// // //
	auto it = muted_.find(chan);
	return it != muted_.end() ? it->second : true;
}

bool CSoundGen::ShouldStopPlayer() const {
	CSingleLock l(&m_csRenderer); l.Lock();		// // //
	return is_rendering_impl() && m_pWaveRenderer->ShouldStopPlayer();
}

int CSoundGen::GetArpNote(stChannelID chan) const {
	if (FTEnv.GetSettings()->Midi.bMidiArpeggio && m_pArpeggiator)		// // //
		return m_pArpeggiator->GetNextNote(chan);
	return -1;
}

std::string CSoundGen::RecallChannelState(stChannelID Channel) const		// // //
{
	if (IsPlaying())
		return m_pSoundDriver->GetChannelStateString(Channel);

	auto [Frame, Row] = m_pTrackerView->GetSelectedPos();
	CSongState state;
	state.Retrieve(*m_pModule, GetPlayerTrack(), Frame, Row);
	return state.GetChannelStateString(*m_pModule, Channel);
}

void CSoundGen::HaltPlayer() {
	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);

	// Move player to non-playing state

	MakeSilent();

	// Signal that playback has stopped
	if (m_pTrackerView)
		m_pInstRecorder->StopRecording(m_pTrackerView);		// // //

	m_pSoundDriver->StopPlayer();		// // //
	m_bHaltRequest = false;
	m_bPlayingSingleRow = false;		// // //
	m_pTempoDisplay.reset();		// // //

#ifdef WRITE_VGM		// // //
	if (m_pVGMWriter) {
		m_pVGMWriter->SaveVGMFile();
		m_pVGMWriter.reset();
	}
#endif
}

void CSoundGen::ResetAPU()
{
	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);

	// Reset the APU
	m_pAPU->Reset();

	// Enable all channels
	m_pAPU->Write(0x4015, 0x0F);
	m_pAPU->Write(0x4017, 0x00);
	m_pAPU->Write(0x4023, 0x02);		// // // FDS enable

	// MMC5
	m_pAPU->Write(0x5015, 0x03);
}

uint8_t CSoundGen::GetReg(sound_chip_t Chip, int Reg) const
{
	return m_pAPU->GetReg(Chip, Reg);
}

CRegisterState *CSoundGen::GetRegState(sound_chip_t Chip, unsigned Reg) const		// // //
{
	return m_pAPU->GetRegState(Chip, Reg);
}

double CSoundGen::GetChannelFrequency(sound_chip_t Chip, int Channel) const		// // //
{
	return m_pAPU->GetFreq(Chip, Channel);
}

void CSoundGen::MakeSilent()
{
	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);

	if (m_pTrackerView)		// // //
		m_pTrackerView->MakeSilent();
	*m_pArpeggiator = CArpeggiator { };

	m_pAPU->Reset();
	m_pSoundDriver->ResetTracks();		// // //
}

void CSoundGen::ResetState()
{
	// Called when a new module is loaded
	m_iLastTrack = 0;		// // //
}

// Get tempo values from the document
void CSoundGen::ResetTempo()
{
	ASSERT(m_pModule);

	if (!m_pModule)
		return;

	const CSongData &song = *m_pModule->GetSong(m_iLastTrack);
	m_pTempoCounter->LoadTempo(song);		// // //
	m_iLastHighlight = song.GetRowHighlight().First;		// // //
}

void CSoundGen::SetHighlightRows(int Rows)		// // //
{
	m_iLastHighlight = Rows;
}

// Return current tempo setting in BPM
double CSoundGen::GetAverageBPM() const		// // // 050B
{
	return m_pTempoDisplay ? m_pTempoDisplay->GetAverageBPM() : m_pTempoCounter->GetTempo();
}

float CSoundGen::GetCurrentBPM() const		// // //
{
	double Max = m_pModule->GetFrameRate() * 15.;
	double BPM = GetAverageBPM();		// // // 050B
	return static_cast<float>((BPM > Max ? Max : BPM) * 4. / (m_iLastHighlight ? m_iLastHighlight : 4));
}

// // //

bool CSoundGen::IsPlaying() const {
	return m_pSoundDriver && m_pSoundDriver->IsPlaying();		// // //
}

CTrackerChannel *CSoundGen::GetTrackerChannel(stChannelID chan) {		// // //
	return m_pSoundDriver->GetTrackerChannel(chan);
}

const CTrackerChannel *CSoundGen::GetTrackerChannel(stChannelID chan) const {
	return m_pSoundDriver->GetTrackerChannel(chan);
}

CArpeggiator &CSoundGen::GetArpeggiator() {		// // //
	return *m_pArpeggiator;
}

void CSoundGen::LoadMachineSettings()		// // //
{
	// Setup machine-type and speed
	//
	// Machine = NTSC or PAL
	//
	// Rate = frame rate (0 means machine default)
	//

	// Called from main thread
	ASSERT(GetCurrentThreadId() == FTEnv.GetMainApp()->m_nThreadID);
	ASSERT(m_pAPU != NULL);

	m_iMachineType = m_pModule->GetMachine();		// // // 050B

	int BaseFreq	= (m_iMachineType == machine_t::NTSC) ? MASTER_CLOCK_NTSC : MASTER_CLOCK_PAL;

	// Choose a default rate if not predefined
	int Rate = m_pModule->GetFrameRate();		// // //

	// Number of cycles between each APU update
	m_iUpdateCycles = BaseFreq / Rate;

	{
		CSingleLock l(&m_csAPULock, TRUE);		// // //
		m_pAPU->ChangeMachineRate(m_iMachineType, Rate);		// // //
	}
}

void CSoundGen::LoadSoundConfig() {		// // //
	ASSERT(GetCurrentThreadId() == FTEnv.GetMainApp()->m_nThreadID);

	LoadSettings();
	Interrupt();
	static_cast<CFrameWnd *>(AfxGetMainWnd())->SetMessageText(IDS_NEW_SOUND_CONFIG);
}

stDPCMState CSoundGen::GetDPCMState() const
{
	if (m_pAPU)		// // //
		if (auto *p2A03 = dynamic_cast<C2A03 *>(m_pAPU->GetSoundChip(sound_chip_t::APU)))		// // //
			return {p2A03->GetSamplePos(), p2A03->GetDeltaCounter()};
	return { };
}

int CSoundGen::GetChannelNote(stChannelID chan) const {		// // //
	return m_pSoundDriver ? m_pSoundDriver->GetChannelNote(chan) : -1;
}

int CSoundGen::GetChannelVolume(stChannelID chan) const {		// // //
	return m_pSoundDriver ? m_pSoundDriver->GetChannelVolume(chan) : 0;
}

// File rendering functions

bool CSoundGen::RenderToFile(const fs::path &fname, std::shared_ptr<CWaveRenderer> pRender)		// // //
{
	// Called from main thread
	ASSERT(GetCurrentThreadId() == FTEnv.GetMainApp()->m_nThreadID);
	ASSERT(m_pDocument != NULL);

	if (!pRender)
		return false;

	if (IsPlaying()) {
		//HaltPlayer();
		m_bHaltRequest = true;
		WaitForStop();
	}

	CSingleLock l(&m_csRenderer); l.Lock();
	m_pWaveRenderer = std::move(pRender);		// // //

	if (_stricmp(PathFindExtensionA(fname.string().c_str()), ".vgm") == 0)	//sh8bit
	{
		m_bRenderingWave = false;
	}
	else
	{
		m_bRenderingWave = true;
	}

	if (m_bRenderingWave)
	{
		ASSERT(!m_pRenderFile);
		m_pRenderFile = std::make_shared<CSimpleFile>(fname, std::ios::out | std::ios::binary);		// // //
		if (m_pRenderFile) {
			m_pWaveRenderer->SetOutputStream(std::make_unique<COutputWaveStream>(m_pRenderFile, CWaveFileFormat{
				CWaveFileFormat::format_code::pcm,
				1,
				static_cast<std::uint32_t>(FTEnv.GetSettings()->Sound.iSampleRate),
				static_cast<std::uint16_t>(FTEnv.GetSettings()->Sound.iSampleSize),
				}));
			PostThreadMessageW(WM_USER_START_RENDER, 0, 0);
			return true;
		}
	}
	else
	{
		VGMStartLogging(fname.string().c_str());
		PostThreadMessageW(WM_USER_START_RENDER, 0, 0);
		return true;
	}

	StopPlayer();
	AfxMessageBox(IDS_FILE_OPEN_ERROR);
	return false;
}

// // //
void CSoundGen::StartRendering() {
	ResetBuffer();
	CSingleLock l(&m_csRenderer); l.Lock();
	m_pWaveRenderer->Start();
}

void CSoundGen::StopRendering()
{
	// Called from player thread
	ASSERT(GetCurrentThreadId() == m_nThreadID);

//	CSingleLock l(&m_csRenderer); l.Lock();
	if (!is_rendering_impl())
		return;

	m_pWaveRenderer.reset();		// // //
	m_pRenderFile.reset();		// // //
	VGMStopLogging(); //sh8bit
	ResetBuffer();
	HaltPlayer();		// // //
	ResetAPU();		// // //
}

bool CSoundGen::IsRendering() const
{
	CSingleLock l(&m_csRenderer); l.Lock();		// // //
	return is_rendering_impl();
}

bool CSoundGen::is_rendering_impl() const {		// // //
	return m_pWaveRenderer && m_pWaveRenderer->Started() && !m_pWaveRenderer->Finished();		// // //
}

bool CSoundGen::IsBackgroundTask() const
{
	return IsRendering();
}

// DPCM handling

void CSoundGen::PlayPreviewSample(int Offset, int Pitch) {		// // //
	int Loop = 0;
	int Length = ((m_pPreviewSample->size() - 1) >> 4) - (Offset << 2);

	if (auto *p2A03 = dynamic_cast<C2A03 *>(m_pAPU->GetSoundChip(sound_chip_t::APU)))		// // //
		p2A03->WriteSample(std::move(m_pPreviewSample));		// // //

	m_pAPU->Write(0x4010, Pitch | Loop);
	m_pAPU->Write(0x4012, Offset);			// load address, start at $C000
	m_pAPU->Write(0x4013, Length);			// length
	m_pAPU->Write(0x4015, 0x0F);
	m_pAPU->Write(0x4015, 0x1F);			// fire sample
}

bool CSoundGen::PreviewDone() const
{
	if (m_pAPU)		// // //
		if (auto *p2A03 = dynamic_cast<C2A03 *>(m_pAPU->GetSoundChip(sound_chip_t::APU)))		// // //
			return !p2A03->DPCMPlaying();
	return true;
}

bool CSoundGen::WaitForStop() const
{
	// Wait for player to stop, timeout = 4s
	// The player must have received the stop command or this will fail

	ASSERT(GetCurrentThreadId() != m_nThreadID);

	//return ::WaitForSingleObject(m_hIsPlaying, 4000) == WAIT_OBJECT_0;

	for (int i = 0; i < 40 && IsPlaying(); ++i)
		Sleep(100);

	return !IsPlaying();	// return false if still playing
}

//
// Overloaded functions
//

BOOL CSoundGen::InitInstance()
{
	//
	// Setup the sound player object, called when thread is started
	//

	ASSERT(m_pDocument != NULL);
//	ASSERT(m_pTrackerView != NULL);

	// First check if thread creation should be cancelled
	// This will occur when no sound object is available

	if (m_pDSound == NULL)
		return FALSE;

	// Set running flag
	m_bRunning = true;

	if (!ResetAudioDevice()) {
		TRACE(L"SoundGen: Failed to reset audio device!\n");
		if (m_pVisualizerWnd != NULL)
			m_pVisualizerWnd->ReportAudioProblem();
	}

	ResetAPU();

	TRACE(L"SoundGen: Created thread (0x%04x)\n", m_nThreadID);

	SetThreadPriority(THREAD_PRIORITY_TIME_CRITICAL);

	m_iFrameCounter = 0;

//	SetupChannels();

	return TRUE;
}

int CSoundGen::ExitInstance()
{
	// Shutdown the thread

	TRACE(L"SoundGen: Closing thread (0x%04x)\n", m_nThreadID);

	// Make sure sound interface is shut down
	CloseAudio();

	m_bRunning = false;

	return CWinThread::ExitInstance();
}

BOOL CSoundGen::OnIdle(LONG lCount)
{
	//
	// Main loop for audio playback thread
	//

	if (CWinThread::OnIdle(lCount))
		return TRUE;

	return IdleLoop();		// // //
}

BOOL CSoundGen::IdleLoop() {
	if (!IsAudioReady())		// // //
		return TRUE;

	++m_iFrameCounter;

	// Access the document object, skip if access wasn't granted to avoid gaps in audio playback
	m_pDocument->Locked([this] {
		m_pSoundDriver->Tick();		// // //
	}, 0);

	m_pSoundDriver->ForeachTrack([&] (CChannelHandler &, CTrackerChannel &TrackerChan, stChannelID ID) {		// // //
		TrackerChan.SetVolumeMeter(m_pAPU->GetVol(ID));		// // //
	});

	if (FTEnv.GetSettings()->Midi.bMidiArpeggio && m_pArpeggiator)		// // //
		m_pArpeggiator->Tick(m_pTrackerView->GetSelectedChannelID());

	// Rendering
	if (CSingleLock l(&m_csRenderer); l.Lock() && m_pWaveRenderer)		// // //
		if (m_pWaveRenderer->ShouldStopRender())
			StopRendering();
		else if (m_pWaveRenderer->ShouldStartPlayer()) {
			int track = m_pWaveRenderer->GetRenderTrack();
			StartPlayer(std::make_unique<CPlayerCursor>(*m_pModule->GetSong(track), track));
		}

	// Update APU registers
	UpdateAPU();

	if (IsPlaying())		// // //
		if (stChannelID Channel = m_pInstRecorder->GetRecordChannel(); Channel.Chip != sound_chip_t::none)		// // //
			m_pInstRecorder->RecordInstrument(GetPlayerTicks(), m_pTrackerView);

	if (m_pSoundDriver->ShouldHalt() || m_bHaltRequest) {		// // //
		// Halt has been requested, abort playback here
		HaltPlayer();
	}

	return TRUE;
}

void CSoundGen::UpdateAPU()
{
	// Copy wave changed flag
	m_bInternalWaveChanged = m_bWaveChanged;
	m_bWaveChanged = false;

	if (CSingleLock l(&m_csAPULock); l.Lock()) {
		// Update APU channel registers
		int cycles = m_iUpdateCycles;
		sound_chip_t LastChip = sound_chip_t::none;		// // // 050B

		m_pSoundDriver->ForeachTrack([&] (CChannelHandler &Chan, CTrackerChannel &, stChannelID ID) {		// // //
			if (m_pModule->GetChannelOrder().HasChannel(ID)) {
				int Delay = (ID.Chip == LastChip) ? 150 : 250;
				if (Delay < cycles) {
					// Add APU cycles
					cycles -= Delay;
					m_pAPU->AddTime(Delay);
				}
				LastChip = ID.Chip;
			}
			m_pAPU->Process();
		});

		// Finish the audio frame
		m_pAPU->AddTime(cycles);
		m_pAPU->Process();
		m_pAPU->EndFrame();		// // //

		VGMLogFrame();	//sh8bit

#ifdef WRITE_VGM		// // //
		if (m_pVGMWriter)
			m_pVGMWriter->Tick();		// // //
#endif
	}

#ifdef LOGGING
	if (m_bPlaying)
		m_pAPU->Log();
#endif
}

// End of overloaded functions

// Thread message handler

void CSoundGen::OnStartPlayer(WPARAM wParam, LPARAM lParam)
{
	BeginPlayer(std::unique_ptr<CPlayerCursor> {reinterpret_cast<CPlayerCursor *>(wParam)});
}

void CSoundGen::OnSilentAll(WPARAM wParam, LPARAM lParam)
{
	MakeSilent();
}

void CSoundGen::OnLoadSettings(WPARAM wParam, LPARAM lParam)
{
	if (!ResetAudioDevice()) {
		TRACE(L"SoundGen: Failed to reset audio device!\n");
		if (m_pVisualizerWnd != NULL)
			m_pVisualizerWnd->ReportAudioProblem();
	}
}

void CSoundGen::OnStopPlayer(WPARAM wParam, LPARAM lParam)
{
	HaltPlayer();
}

void CSoundGen::OnResetPlayer(WPARAM wParam, LPARAM lParam)
{
	// Called when the selected song has changed

	auto pCur = std::unique_ptr<CPlayerCursor> {reinterpret_cast<CPlayerCursor *>(wParam)};		// // //
	m_iLastTrack = pCur->GetCurrentSong();
	if (IsPlaying())
		BeginPlayer(std::move(pCur));		// // //
}

void CSoundGen::OnStartRender(WPARAM wParam, LPARAM lParam)
{
	StartRendering();		// // //
}

void CSoundGen::OnStopRender(WPARAM wParam, LPARAM lParam)
{
	CSingleLock l(&m_csRenderer); l.Lock();
	StopRendering();
}

void CSoundGen::OnPreviewSample(WPARAM wParam, LPARAM lParam)
{
	PlayPreviewSample(wParam, lParam);
}

void CSoundGen::OnWriteAPU(WPARAM wParam, LPARAM lParam)
{
	m_pAPU->Write((uint16_t)wParam, (uint8_t)lParam);
}

void CSoundGen::OnCloseSound(WPARAM wParam, LPARAM lParam)
{
	CloseAudio();

	// Notification
	CEvent *pEvent = (CEvent*)wParam;
	if (pEvent != NULL && pEvent->IsKindOf(RUNTIME_CLASS(CEvent)))
		pEvent->SetEvent();
}

void CSoundGen::OnSetChip(WPARAM wParam, LPARAM lParam)
{
	auto Chip = CSoundChipSet::FromFlag(wParam);		// // //

	m_pAPU->SetExternalSound(Chip);

	// Enable internal channels after reset
	if (Chip.ContainsChip(sound_chip_t::APU)) {
		m_pAPU->Write(0x4015, 0x0F);
		m_pAPU->Write(0x4017, 0x00);
	}

	// MMC5
	if (Chip.ContainsChip(sound_chip_t::MMC5))
		m_pAPU->Write(0x5015, 0x03);
}

void CSoundGen::OnRemoveDocument(WPARAM wParam, LPARAM lParam)
{
	// Remove document and view pointers
	m_pModule = nullptr;		// // //
	m_pDocument = nullptr;
	m_pTrackerView = nullptr;
	m_pInstRecorder->SetDumpCount(0);		// // //
	m_pInstRecorder->ReleaseCurrent();
	// m_pInstRecorder->ResetDumpInstrument();
	//if (*m_pDumpInstrument)		// // //
	//	(*m_pDumpInstrument)->Release();
	m_pInstRecorder->ResetRecordCache();
	TRACE(L"SoundGen: Document removed\n");
}

// FDS & N163

void CSoundGen::WaveChanged()
{
	// Call when FDS or N163 wave is altered from the instrument editor
	m_bWaveChanged = true;
}

bool CSoundGen::HasWaveChanged() const
{
	return m_bInternalWaveChanged;
}

void CSoundGen::SetNamcoMixing(bool bLinear)		// // //
{
	m_pAPU->SetNamcoMixing(bLinear);
}

// Player state functions

void CSoundGen::QueueNote(stChannelID Channel, const stChanNote &NoteData, note_prio_t Priority) const		// // //
{
	// Queue a note for play
	m_pSoundDriver->QueueNote(Channel, NoteData, Priority);
	FTEnv.GetMIDI()->WriteNote((uint8_t)m_pModule->GetChannelOrder().GetChannelIndex(Channel), NoteData.Note, NoteData.Octave, NoteData.Vol);
}

void CSoundGen::ForceReloadInstrument(stChannelID Channel)		// // //
{
	m_pSoundDriver->ForceReloadInstrument(Channel);
}

std::pair<unsigned, unsigned> CSoundGen::GetPlayerPos() const {		// // //
	if (const auto &cursor = m_pSoundDriver->GetPlayerCursor())
		return std::make_pair(cursor->GetCurrentFrame(), cursor->GetCurrentRow());
	return std::make_pair(0, 0);
}

int CSoundGen::GetPlayerTrack() const
{
	return m_iLastTrack;
}

int CSoundGen::GetPlayerTicks() const
{
	if (const auto &cursor = m_pSoundDriver->GetPlayerCursor())		// // //
		return cursor->GetTotalTicks();
	return 0;
}

void CSoundGen::MoveToFrame(int Frame)
{
	// Todo: synchronize
	if (auto cursor = m_pSoundDriver->GetPlayerCursor())
		cursor->SetPosition(Frame, 0);		// // //
}

void CSoundGen::SetQueueFrame(unsigned Frame)
{
	if (auto cursor = m_pSoundDriver->GetPlayerCursor())
		cursor->QueueFrame(Frame);		// // //
}

unsigned CSoundGen::GetQueueFrame() const
{
	if (const auto &cursor = m_pSoundDriver->GetPlayerCursor())
		return cursor->GetQueuedFrame().value_or(-1);
	return -1;
}

// Verification

CInstrumentManager *CSoundGen::GetInstrumentManager() const
{
	return m_pModule ? m_pModule->GetInstrumentManager() : nullptr;
}

void CSoundGen::SetSequencePlayPos(std::shared_ptr<const CSequence> pSequence, int Pos) {		// // //
	if (pSequence == m_pSequencePlayPos) {
		m_iSequencePlayPos = Pos;
		m_iSequenceTimeout = 5;
	}
}

int CSoundGen::GetSequencePlayPos(std::shared_ptr<const CSequence> pSequence)		// // //
{
	if (m_pSequencePlayPos != pSequence)
		m_iSequencePlayPos = -1;

	if (m_iSequenceTimeout == 0)
		m_iSequencePlayPos = -1;
	else
		--m_iSequenceTimeout;

	int Ret = m_iSequencePlayPos;
	m_pSequencePlayPos = std::move(pSequence);
	return Ret;
}

void CSoundGen::SetMeterDecayRate(decay_rate_t Type) const		// // // 050B
{
	m_pAPU->SetMeterDecayRate(Type);
}

decay_rate_t CSoundGen::GetMeterDecayRate() const
{
	return m_pAPU->GetMeterDecayRate();
}

// // // instrument recorder

std::unique_ptr<CInstrument> CSoundGen::GetRecordInstrument() const
{
	return m_pInstRecorder->GetRecordInstrument(GetPlayerTicks());
}

void CSoundGen::ResetDumpInstrument()
{
	m_pInstRecorder->ResetDumpInstrument();
}

stChannelID CSoundGen::GetRecordChannel() const
{
	return m_pInstRecorder->GetRecordChannel();
}

void CSoundGen::SetRecordChannel(stChannelID Channel)
{
	m_pInstRecorder->SetRecordChannel(Channel);
}

const stRecordSetting &CSoundGen::GetRecordSetting() const
{
	return m_pInstRecorder->GetRecordSetting();
}

void CSoundGen::SetRecordSetting(const stRecordSetting &Setting)
{
	m_pInstRecorder->SetRecordSetting(Setting);
}

#include "MainFrm.h"
#include "FamiTrackerDoc.h"
#include "FamiTrackerModule.h"
#include "FamiTrackerEnv.h"

void CSoundGen::VGMStartLogging(const char *Filename)
{
	vgmFile = fopen(Filename, "wb");

	if (!vgmFile) return;

	//prepare VGM header
	
	vgmFrameRate = FTEnv.GetMainFrame()->GetDoc().GetModule()->GetFrameRate();

	int fm_clock = 3579545;

	memset(vgmHeader, 0, sizeof(vgmHeader));

	for (int i = 0; i < 256; ++i) vgmRegPrev[i] = -1;

	vgmHeader[0x00] = 0x56;	//'Vgm ' signature
	vgmHeader[0x01] = 0x67;
	vgmHeader[0x02] = 0x6d;
	vgmHeader[0x03] = 0x20;
	vgmHeader[0x08] = 0x50;	//version 1.50
	vgmHeader[0x09] = 0x01;
	vgmHeader[0x0a] = 0x00;
	vgmHeader[0x0b] = 0x00;
	vgmHeader[0x0c] = 0;	//no PSG
	vgmHeader[0x0d] = 0;
	vgmHeader[0x0e] = 0;
	vgmHeader[0x0f] = 0;
	vgmHeader[0x10] = fm_clock & 255;	//YM2413
	vgmHeader[0x11] = (fm_clock >> 8) & 255;
	vgmHeader[0x12] = (fm_clock >> 16) & 255;
	vgmHeader[0x13] = (fm_clock >> 24) & 255;
	vgmHeader[0x14] = 0;	//no GD3
	vgmHeader[0x15] = 0;
	vgmHeader[0x16] = 0;
	vgmHeader[0x17] = 0;
	vgmHeader[0x24] = vgmFrameRate;
	vgmHeader[0x25] = 0;
	vgmHeader[0x26] = 0;
	vgmHeader[0x27] = 0;
	vgmHeader[0x28] = 0x09;	//noise feedback (SMS)
	vgmHeader[0x29] = 0x00;
	vgmHeader[0x2a] = 16;	//noise register width (SMS)
	vgmHeader[0x2b] = 0;
	vgmHeader[0x2c] = 0;	//no YM2612
	vgmHeader[0x2d] = 0;
	vgmHeader[0x2e] = 0;
	vgmHeader[0x2f] = 0;
	vgmHeader[0x30] = 0;	//no YM2151
	vgmHeader[0x31] = 0;
	vgmHeader[0x32] = 0;
	vgmHeader[0x33] = 0;
	vgmHeader[0x34] = 0x0c;	//offset to VGM data
	vgmHeader[0x35] = 0;
	vgmHeader[0x36] = 0;
	vgmHeader[0x37] = 0;

	fwrite(vgmHeader, sizeof(vgmHeader), 1, vgmFile);

	vgmFrames = 0;
	vgmLoopFrame = 0;
	vgmLoopOffset = 0x40 - 0x1c;
}

void CSoundGen::VGMStopLogging()
{
	if (!vgmFile) return;

	fputc(0x66, vgmFile);	//EOF

	fflush(vgmFile);

	int len = ftell(vgmFile) - 4;

	vgmHeader[0x04] = len & 255;
	vgmHeader[0x05] = (len >> 8) & 255;
	vgmHeader[0x06] = (len >> 16) & 255;
	vgmHeader[0x07] = (len >> 24) & 255;

	int samples = vgmFrames * (44100 / vgmFrameRate);

	vgmHeader[0x18] = samples & 255;
	vgmHeader[0x19] = (samples >> 8) & 255;
	vgmHeader[0x1a] = (samples >> 16) & 255;
	vgmHeader[0x1b] = (samples >> 24) & 255;

	int loop_samples = samples - vgmLoopFrame * (44100 / vgmFrameRate);

	vgmHeader[0x1c] = vgmLoopOffset & 255;
	vgmHeader[0x1d] = (vgmLoopOffset >> 8) & 255;
	vgmHeader[0x1e] = (vgmLoopOffset >> 16) & 255;
	vgmHeader[0x1f] = (vgmLoopOffset >> 24) & 255;

	vgmHeader[0x20] = loop_samples & 255;
	vgmHeader[0x21] = (loop_samples >> 8) & 255;
	vgmHeader[0x22] = (loop_samples >> 16) & 255;
	vgmHeader[0x23] = (loop_samples >> 24) & 255;

	fseek(vgmFile, 0, SEEK_SET);
	fwrite(vgmHeader, sizeof(vgmHeader), 1, vgmFile);

	fclose(vgmFile);

	vgmFile = 0;
}

void CSoundGen::VGMLogOPLLWrite(int reg, int val)
{
	if (!vgmFile) return;
	if (!IsPlaying()) return;

	if (vgmRegPrev[reg] == val) return;	//filter out repeating writes

	vgmRegPrev[reg] = val;

	uint8_t data[3];

	data[0] = 0x51;
	data[1] = reg;
	data[2] = val;

	fwrite(data, sizeof(data), 1, vgmFile);
}

void CSoundGen::VGMLogFrame()
{
	if (!vgmFile) return;
	if (!IsPlaying()) return;

	++vgmFrames;

	if (vgmFrameRate == 60)
	{
		fputc(0x62, vgmFile);//ntsc
	}
	else
	{
		fputc(0x63, vgmFile);//pal
	}
}