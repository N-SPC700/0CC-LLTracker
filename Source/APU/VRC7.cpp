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

#include "APU/VRC7.h"
#include "APU/Mixer.h"		// // //
#include "RegisterState.h"		// // //

const float  CVRC7::AMPLIFY	  = 4.6f;		// Mixing amplification, VRC7 patch 14 is 4,88 times stronger than a 50% square @ v=15
const uint32_t CVRC7::OPL_CLOCK = 3579545;	// Clock frequency

CVRC7::CVRC7(CMixer &Mixer, std::uint8_t nInstance) : CSoundChip(Mixer, nInstance)
{
	m_pRegisterLogger->AddRegisterRange(0x00, 0x07);		// // //
	m_pRegisterLogger->AddRegisterRange(0x0e, 0x0e);
	m_pRegisterLogger->AddRegisterRange(0x10, 0x18);
	m_pRegisterLogger->AddRegisterRange(0x20, 0x28);
	m_pRegisterLogger->AddRegisterRange(0x30, 0x38);
	Reset();
}

sound_chip_t CVRC7::GetID() const {		// // //
	return sound_chip_t::VRC7;
}

void CVRC7::Reset()
{
	m_iBufferPtr = 0;
	m_iTime = 0;
}

void CVRC7::SetSampleSpeed(uint32_t SampleRate, double ClockRate, uint32_t FrameRate)
{
	m_pOPLLInt.reset(OPLL_new(OPL_CLOCK, SampleRate));		// // //

	OPLL_reset(m_pOPLLInt.get());
	OPLL_reset_patch(m_pOPLLInt.get(), 1);

	m_iMaxSamples = (SampleRate / FrameRate) * 2;	// Allow some overflow

	m_iBuffer = std::vector<int16_t>(m_iMaxSamples);		// // //
}

void CVRC7::SetVolume(float Volume)
{
	m_fVolume = Volume * AMPLIFY;
}

#include "FamiTrackerEnv.h"	//sh8bit
#include "SoundGen.h"

void CVRC7::Write(uint16_t Address, uint8_t Value)
{
	switch (Address) {
		case 0x9010:
			m_iSoundReg = Value;
			break;
		case 0x9030:
			OPLL_writeReg(m_pOPLLInt.get(), m_iSoundReg, Value);
			FTEnv.GetSoundGenerator()->VGMLogOPLLWrite(m_iSoundReg, Value);//sh8bit
			break;
	}
}

void CVRC7::Log(uint16_t Address, uint8_t Value)		// // //
{
	switch (Address) {
	case 0x9010: m_pRegisterLogger->SetPort(Value); break;
	case 0x9030: m_pRegisterLogger->Write(Value); break;
	}
}

uint8_t CVRC7::Read(uint16_t Address, bool &Mapped)
{
	return 0;
}

void CVRC7::EndFrame()
{
	uint32_t WantSamples = m_pMixer->GetMixSampleCount(m_iTime);

	static int32_t LastSample = 0;

	// Generate VRC7 samples
	while (m_iBufferPtr < WantSamples) {
		int32_t RawSample = OPLL_calc(m_pOPLLInt.get());

		// Clipping is slightly asymmetric
		if (RawSample > 3600)
			RawSample = 3600;
		if (RawSample < -3200)
			RawSample = -3200;

		// Apply volume
		int32_t Sample = int(float(RawSample) * m_fVolume);

		if (Sample > 32767)
			Sample = 32767;
		if (Sample < -32768)
			Sample = -32768;

		m_iBuffer[m_iBufferPtr++] = int16_t((Sample + LastSample) >> 1);
		LastSample = Sample;
	}

	m_pMixer->MixSamples((blip_sample_t*)m_iBuffer.data(), WantSamples);		// // //

	m_iBufferPtr -= WantSamples;
	m_iTime = 0;
}

void CVRC7::Process(uint32_t Time)
{
	// This cannot run in sync, fetch all samples at end of frame instead
	m_iTime += Time;
}

double CVRC7::GetFreq(int Channel) const		// // //
{
	if (Channel < 0 || Channel >= 9) return 0.;
	int Lo = m_pRegisterLogger->GetRegister(Channel | 0x10)->GetValue();
	int Hi = m_pRegisterLogger->GetRegister(Channel | 0x20)->GetValue() & 0x0F;
	Lo |= (Hi << 8) & 0x100;
	Hi >>= 1;
	return 49716. * Lo / (1 << (19 - Hi));
}
