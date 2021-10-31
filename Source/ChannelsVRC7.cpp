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

// This file handles playing of VRC7 channels

#include "ChannelsVRC7.h"
#include "APU/Types.h"		// // //
#include "APU/APUInterface.h"		// // //
#include "Instrument.h"		// // //
#include "InstHandler.h"		// // //
#include "InstHandlerVRC7.h"		// // //
#include "ChipHandlerVRC7.h"		// // //

extern int g_iPercMode;	//sh8bit
extern int g_iPercModePrev;
extern int g_iPercVolumeBD;
extern int g_iPercVolumeSDHH;
extern int g_iPercVolumeTOMCY;

namespace {

	const int OPL_NOTE_ON = 0x10;
	const int OPL_SUSTAIN_ON = 0x20;

	const int VRC7_PITCH_RESOLUTION = 2;		// // // extra bits for internal pitch

} // namespace

CChannelHandlerVRC7::CChannelHandlerVRC7(stChannelID ch, CChipHandlerVRC7 &parent) :		// // //
	CChannelHandlerInverted(ch, (1 << (VRC7_PITCH_RESOLUTION + 9)) - 1, 15),		// // //
	chip_handler_(parent)
{
	m_iVolume = VOL_COLUMN_MAX;

	g_iPercMode = 0;//sh8bit
	g_iPercModePrev = 0;
	g_iPercVolumeBD = 15;
	g_iPercVolumeSDHH = 15;
	g_iPercVolumeTOMCY = 15;
}

void CChannelHandlerVRC7::SetPatch(unsigned char Patch)		// // //
{
	m_iDutyPeriod = Patch;
}

void CChannelHandlerVRC7::SetCustomReg(size_t Index, unsigned char Val)		// // //
{
	chip_handler_.SetPatchReg(Index & 0x07u, Val);		// // //
}

void CChannelHandlerVRC7::HandleNoteData(stChanNote &NoteData)		// // //
{
	CChannelHandlerInverted::HandleNoteData(NoteData);		// // //

	if (m_iCommand == CMD_NOTE_TRIGGER && NoteData.Instrument == HOLD_INSTRUMENT)		// // // 050B
		m_iCommand = CMD_NOTE_ON;
}

bool CChannelHandlerVRC7::HandleEffect(stEffectCommand cmd)
{
	switch (cmd.fx) {
	case effect_t::DUTY_CYCLE:
		m_iPatch = cmd.param;		// // // 050B
		break;
	case effect_t::VRC7_PORT:		// // // 050B
		m_iCustomPort = cmd.param & 0x07;
		break;
	case effect_t::VRC7_WRITE:		// // // 050B
		chip_handler_.QueuePatchReg(m_iCustomPort, cmd.param);		// // //
		break;
	case effect_t::VRC7_PERCUSSION:	//sh8bit
		switch (cmd.param & 0xf0)
		{
		case 0x00://turn percussion mode on/off
			switch (cmd.param & 0x0f)
			{
			case 0x00://off
				g_iPercMode &= ~0x20;
				break;
			case 0x01://on
				g_iPercMode |= 0x20;
				break;
			}
			break;
		}
		break;
	default: return CChannelHandlerInverted::HandleEffect(cmd);
	}

	return true;
}

void CChannelHandlerVRC7::HandleEmptyNote()
{
}

void CChannelHandlerVRC7::HandleCut()
{
	RegisterKeyState(-1);
	m_bGate = false;
//	m_iPeriod = 0;
//	m_iPortaTo = 0;
	m_iCommand = CMD_NOTE_HALT;
//	m_iOldOctave = -1;		// // //
}

void CChannelHandlerVRC7::UpdateNoteRelease()		// // //
{
	// Note release (Lxx)
	if (m_iNoteRelease > 0) {
		--m_iNoteRelease;
		if (m_iNoteRelease == 0) {
			HandleRelease();
		}
	}
}

void CChannelHandlerVRC7::HandleRelease()
{
	if (!m_bRelease) {
		m_iCommand = CMD_NOTE_RELEASE;
		RegisterKeyState(-1);
	}
}

void CChannelHandlerVRC7::HandleNote(int MidiNote)
{
	CChannelHandlerInverted::HandleNote(MidiNote);		// // //

	m_bHold = true;

/*
	if ((m_iEffect != effect_t::PORTAMENTO || m_iPortaSpeed == 0) ||
		m_iCommand == CMD_NOTE_HALT || m_iCommand == CMD_NOTE_RELEASE)		// // // 050B
		m_iCommand = CMD_NOTE_TRIGGER;
*/
	if (m_iPortaSpeed > 0 && m_iEffect == effect_t::PORTAMENTO &&
		m_iCommand != CMD_NOTE_HALT && m_iCommand != CMD_NOTE_RELEASE)		// // // 050B
		CorrectOctave();
	else
		m_iCommand = CMD_NOTE_TRIGGER;
}

void CChannelHandlerVRC7::RunNote(int MidiNote) {		// // //
	// Run the note and handle portamento
	int Octave = ft0cc::doc::oct_from_midi(MidiNote);

	int NesFreq = TriggerNote(MidiNote);

	if (m_iPortaSpeed > 0 && m_iEffect == effect_t::PORTAMENTO && m_bGate) {		// // //
		if (m_iPeriod == 0) {
			m_iPeriod = NesFreq;
			m_iOldOctave = m_iOctave = Octave;
		}
		m_iPortaTo = NesFreq;

	}
	else {
		m_iPeriod = NesFreq;
		m_iPortaTo = 0;
		m_iOldOctave = m_iOctave = Octave;
	}

	m_bGate = true;

	CorrectOctave();		// // //
}

bool CChannelHandlerVRC7::CreateInstHandler(inst_type_t Type)
{
	switch (Type) {
	case INST_VRC7:
		if (m_iInstTypeCurrent != INST_VRC7)
			m_pInstHandler = std::make_unique<CInstHandlerVRC7>(this, 0x0F);
		return true;
	}
	return false;
}

void CChannelHandlerVRC7::SetupSlide()		// // //
{
	CChannelHandler::SetupSlide();		// // //

	CorrectOctave();
}

void CChannelHandlerVRC7::CorrectOctave()		// // //
{
	// Set current frequency to the one with highest octave
	if (m_bLinearPitch)
		return;

	if (m_iOldOctave == -1) {
		m_iOldOctave = m_iOctave;
		return;
	}

	int Offset = m_iOctave - m_iOldOctave;
	if (Offset > 0) {
		m_iPeriod >>= Offset;
		m_iOldOctave = m_iOctave;
	}
	else if (Offset < 0) {
		// Do nothing
		m_iPortaTo >>= -Offset;
		m_iOctave = m_iOldOctave;
	}
}

int CChannelHandlerVRC7::TriggerNote(int Note)
{
	m_iTriggeredNote = Note;
	RegisterKeyState(Note);
	if (m_iCommand != CMD_NOTE_TRIGGER && m_iCommand != CMD_NOTE_HALT)
		m_iCommand = CMD_NOTE_ON;
	m_iOctave = Note / NOTE_RANGE;

	if (g_iPercMode & 0x20)//sh8bit
	{
		if (GetChannelID().Subindex >= 6)
		{
			switch (Note % 12)	//drum mapping similar to the MIDI drum layout
			{
			case 0:	//BD
			case 1:
				g_iPercMode |= 0x10;
				g_iPercVolumeBD = (15 - CalculateVolume());
				break;
			case 2: //SD
			case 3:
			case 4:
				g_iPercMode |= 0x08;
				g_iPercVolumeSDHH = (g_iPercVolumeSDHH & 0xf0) | (15 - CalculateVolume());
				break;
			case 5: //TOM
			case 7:
			case 9:
			case 11:
				g_iPercMode |= 0x04;
				g_iPercVolumeTOMCY = (g_iPercVolumeTOMCY & 0x0f) | ((15 - CalculateVolume()) << 4);
				break;
			case 10: //CY
				g_iPercMode |= 0x02;
				g_iPercVolumeTOMCY = (g_iPercVolumeTOMCY & 0xf0) | (15 - CalculateVolume());
				break;
			case 6: //HH
			case 8:
				g_iPercMode |= 0x01;
				g_iPercVolumeSDHH = (g_iPercVolumeSDHH & 0x0f) | ((15 - CalculateVolume()) << 4);
				break;
			}
		}
	}

	return m_bLinearPitch ? (Note << LINEAR_PITCH_AMOUNT) : GetFnum(Note);		// // //
}

unsigned int CChannelHandlerVRC7::GetFnum(int Note) const
{
	return m_iNoteLookupTable[Note % NOTE_RANGE] << VRC7_PITCH_RESOLUTION;		// // //
}

int CChannelHandlerVRC7::CalculateVolume() const
{
	int Volume = (m_iVolume >> VOL_COLUMN_SHIFT) - GetTremolo();
	if (Volume > 15)
		Volume = 15;
	if (Volume < 0)
		Volume = 0;
	return Volume;		// // //
}

int CChannelHandlerVRC7::CalculatePeriod() const
{
	int Detune = GetVibrato() - GetFinePitch() - GetPitch();
	int Period = LimitPeriod(GetPeriod() + (Detune << VRC7_PITCH_RESOLUTION));		// // //
	if (m_bLinearPitch && !m_iNoteLookupTable.empty()) {
		Period = LimitPeriod(GetPeriod() + Detune);		// // //
		int Note = (Period >> LINEAR_PITCH_AMOUNT) % NOTE_RANGE;
		int Sub = Period % (1 << LINEAR_PITCH_AMOUNT);
		int Offset = (GetFnum(Note + 1) << ((Note < NOTE_RANGE - 1) ? 0 : 1)) - GetFnum(Note);
		Offset = Offset * Sub >> LINEAR_PITCH_AMOUNT;
		if (Sub && Offset < (1 << VRC7_PITCH_RESOLUTION)) Offset = 1 << VRC7_PITCH_RESOLUTION;
		Period = GetFnum(Note) + Offset;
	}
	return LimitRawPeriod(Period) >> VRC7_PITCH_RESOLUTION;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// VRC7 Channels
///////////////////////////////////////////////////////////////////////////////////////////////////////////

void CChannelHandlerVRC7::RefreshChannel()
{
//	int Note = m_iTriggeredNote;
	int Volume = CalculateVolume();
	int Fnum = CalculatePeriod();		// // //
	int Bnum = !m_bLinearPitch ? m_iOctave :
		((GetPeriod() + GetVibrato() - GetFinePitch() - GetPitch()) >> LINEAR_PITCH_AMOUNT) / NOTE_RANGE;

	if (m_iPatch != -1) {		// // //
		m_iDutyPeriod = m_iPatch;
		m_iPatch = -1;
	}

	unsigned subindex = GetChannelID().Subindex;		// // //

	// Write custom instrument
	if (m_iDutyPeriod == 0 && m_iCommand == CMD_NOTE_TRIGGER)		// // //
		chip_handler_.RequestPatchUpdate();

	if (!m_bGate)
		m_iCommand = CMD_NOTE_HALT;

	if (subindex == 8)	//sh8bit
	{
		//only send all percussion related writes from one of the channels

		if (g_iPercMode & 0x20)
		{
			//repeating writes will get filtered out during export

			RegWrite(0x26, 0x00);	//force key off to percussion channels
			RegWrite(0x27, 0x00);
			RegWrite(0x28, 0x00);
			RegWrite(0x16, 0x20);	//preset values for percussion
			RegWrite(0x17, 0x50);
			RegWrite(0x18, 0xc0);
			RegWrite(0x26, 0x05);
			RegWrite(0x27, 0x05);
			RegWrite(0x28, 0x01);

			RegWrite(0x0e, g_iPercMode);	//enable rhythm mode
			RegWrite(0x36, g_iPercVolumeBD);	//percussion volume
			RegWrite(0x37, g_iPercVolumeSDHH);
			RegWrite(0x38, g_iPercVolumeTOMCY);

			g_iPercMode &= ~0x1f;
		}
		else
		{
			if (g_iPercModePrev & 0x20)
			{
				RegWrite(0x0e, 0x00);	//disable rhythm mode
				RegWrite(0x26, 0x00);	//force key off to percussion channels
				RegWrite(0x27, 0x00);
				RegWrite(0x28, 0x00);
				RegWrite(0x36, 0x1f);
				RegWrite(0x37, 0x1f);
				RegWrite(0x38, 0x1f);
				
			}
		}

		g_iPercModePrev = g_iPercMode;
	}

	if ((g_iPercMode & 0x20) && (subindex >= 6)) return;	//don't allow notes on the percussion channels when percussion mode is enabled

	int Cmd = 0;

	switch (m_iCommand) {
	case CMD_NOTE_TRIGGER:
		RegWrite(0x20 + subindex, 0);
		m_iCommand = CMD_NOTE_ON;
		Cmd = OPL_NOTE_ON | OPL_SUSTAIN_ON;
		break;
	case CMD_NOTE_ON:
		Cmd = m_bHold ? OPL_NOTE_ON : OPL_SUSTAIN_ON;
		break;
	case CMD_NOTE_HALT:
		Cmd = 0;
		break;
	case CMD_NOTE_RELEASE:
		Cmd = OPL_SUSTAIN_ON;
		break;
	}

	// Write frequency
	RegWrite(0x10 + subindex, Fnum & 0xFF);

	if (m_iCommand != CMD_NOTE_HALT) {
		// Select volume & patch
		RegWrite(0x30 + subindex, (m_iDutyPeriod << 4) | (Volume ^ 0x0F));		// // //
	}

	RegWrite(0x20 + subindex, ((Fnum >> 8) & 1) | (Bnum << 1) | Cmd);
}

void CChannelHandlerVRC7::ClearRegisters()
{
	unsigned subindex = GetChannelID().Subindex;		// // //
	RegWrite(0x10 + subindex, 0x00);
	RegWrite(0x20 + subindex, 0x00);
	RegWrite(0x30 + subindex, 0x0F);

	m_iNote = -1;
	m_iOctave = m_iOldOctave = -1;		// // //
	m_iPatch = -1;
	m_iEffect = effect_t::none;

	m_iCommand = CMD_NOTE_HALT;
	m_iCustomPort = 0;		// // // 050B
}

void CChannelHandlerVRC7::RegWrite(unsigned char Reg, unsigned char Value)
{
	m_pAPU->Write(0x9010, Reg);
	m_pAPU->Write(0x9030, Value);
}
