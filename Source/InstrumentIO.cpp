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

#include "InstrumentIO.h"
#include "Instrument2A03.h"
#include "InstrumentVRC7.h"
#include "InstrumentFDS.h"
#include "InstrumentN163.h"

#include "NumConv.h"

#include "InstrumentManagerInterface.h"
#include "Sequence.h"
#include "OldSequence.h"
#include "ft0cc/doc/dpcm_sample.hpp"

#include "DocumentFile.h"
#include "SimpleFile.h"



namespace {

// FTI instruments files
const std::string_view FTI_INST_HEADER = "LTI";
const std::string_view FTI_INST_VERSION = "2.4";

} // namespace

CInstrumentIO::CInstrumentIO(module_error_level_t err_lv) : err_lv_(err_lv) {
}

void CInstrumentIO::WriteToModule(const CInstrument &inst, CDocumentFile &file, unsigned inst_index) const {
	// Write index and type
	file.WriteBlockInt(inst_index);
	file.WriteBlockChar(static_cast<char>(inst.GetType()));

	// Store the instrument
	DoWriteToModule(inst, file);

	// Store the name
	file.WriteStringCounted(inst.GetName());		// // //
}

void CInstrumentIO::WriteToFTI(const CInstrument &inst, CSimpleFile &file) const {
	// Write header
	file.WriteBytes(FTI_INST_HEADER);
	file.WriteBytes(FTI_INST_VERSION);

	// Write type
	file.WriteInt8(inst.GetType());

	// Write name
	file.WriteString(inst.GetName());

	// Write instrument data
	DoWriteToFTI(inst, file);
}

void CInstrumentIO::ReadFromFTI(CInstrument &inst, CSimpleFile &file, int fti_ver) const {
	inst.SetName(file.ReadString());		// // //
	DoReadFromFTI(inst, file, fti_ver);
}

template <module_error_level_t l, typename T, typename U, typename V>
T CInstrumentIO::AssertRange(T Value, U Min, V Max, const std::string &Desc) const {
	if (l <= err_lv_ && !(Value >= Min && Value <= Max)) {
		std::string msg = Desc + " out of range: expected ["
			+ std::to_string(Min) + ','
			+ std::to_string(Max) + "], got "
			+ std::to_string(Value);
		throw CModuleException::WithMessage(msg);
	}
	return Value;
}



void CInstrumentIONull::DoWriteToModule(const CInstrument &inst_, CDocumentFile &file) const {
}

void CInstrumentIONull::ReadFromModule(CInstrument &inst_, CDocumentFile &file) const {
}

void CInstrumentIONull::DoWriteToFTI(const CInstrument &inst_, CSimpleFile &file) const {
}

void CInstrumentIONull::DoReadFromFTI(CInstrument &inst_, CSimpleFile &file, int fti_ver) const {
}



void CInstrumentIOSeq::DoWriteToModule(const CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<const CSeqInstrument &>(inst_);

	int seqCount = inst.GetSeqCount();
	file.WriteBlockInt(seqCount);

	for (int i = 0; i < seqCount; ++i) {
		file.WriteBlockChar(inst.GetSeqEnable((sequence_t)i) ? 1 : 0);
		file.WriteBlockChar(inst.GetSeqIndex((sequence_t)i));
	}
}

void CInstrumentIOSeq::ReadFromModule(CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<CSeqInstrument &>(inst_);

	AssertRange(file.GetBlockInt(), 0, (int)SEQ_COUNT, "Instrument sequence count"); // unused right now

	for (auto i : enum_values<sequence_t>()) {
		inst.SetSeqEnable(i, 0 != AssertRange<MODULE_ERROR_STRICT>(
			file.GetBlockChar(), 0, 1, "Instrument sequence enable flag"));
		int Index = static_cast<unsigned char>(file.GetBlockChar());
		inst.SetSeqIndex(i, AssertRange(Index, 0, MAX_SEQUENCES - 1, "Instrument sequence index"));
	}
}

void CInstrumentIOSeq::DoWriteToFTI(const CInstrument &inst_, CSimpleFile &file) const {
	auto &inst = dynamic_cast<const CSeqInstrument &>(inst_);

	int seqCount = inst.GetSeqCount();
	file.WriteInt8(static_cast<char>(seqCount));

	for (auto i : enum_values<sequence_t>()) {
		if (inst.GetSeqEnable(i)) {
			auto pSeq = inst.GetSequence(i);
			file.WriteInt8(1);
			file.WriteInt32(pSeq->GetItemCount());
			file.WriteInt32(pSeq->GetLoopPoint());
			file.WriteInt32(pSeq->GetReleasePoint());
			file.WriteInt32(pSeq->GetSetting());
			for (unsigned j = 0; j < pSeq->GetItemCount(); ++j) {
				file.WriteInt8(pSeq->GetItem(j));
			}
		}
		else
			file.WriteInt8(0);
	}
}

void CInstrumentIOSeq::DoReadFromFTI(CInstrument &inst_, CSimpleFile &file, int fti_ver) const {
	auto &inst = dynamic_cast<CSeqInstrument &>(inst_);

	// Sequences
	std::shared_ptr<CSequence> pSeq;

	AssertRange(file.ReadInt8(), 0, (int)SEQ_COUNT, "Sequence count"); // unused right now

	// Loop through all instrument effects
	for (auto i : enum_values<sequence_t>()) {
		try {
			if (file.ReadInt8() != 1) {
				inst.SetSeqEnable(i, false);
				inst.SetSeqIndex(i, 0);
				continue;
			}
			inst.SetSeqEnable(i, true);

			// Read the sequence
			int Count = AssertRange(file.ReadInt32(), 0, 0xFF, "Sequence item count");

			if (fti_ver < 20) {
				COldSequence OldSeq;
				for (int j = 0; j < Count; ++j) {
					char Length = file.ReadInt8();
					OldSeq.AddItem(Length, file.ReadInt8());
				}
				pSeq = OldSeq.Convert(i);
			}
			else {
				pSeq = std::make_shared<CSequence>(i);
				int Count2 = Count > MAX_SEQUENCE_ITEMS ? MAX_SEQUENCE_ITEMS : Count;
				pSeq->SetItemCount(Count2);
				pSeq->SetLoopPoint(AssertRange(
					static_cast<int>(file.ReadInt32()), -1, Count2 - 1, "Sequence loop point"));
				if (fti_ver > 20) {
					pSeq->SetReleasePoint(AssertRange(
						static_cast<int>(file.ReadInt32()), -1, Count2 - 1, "Sequence release point"));
					if (fti_ver >= 22)
						pSeq->SetSetting(static_cast<seq_setting_t>(file.ReadInt32()));
				}
				for (int j = 0; j < Count; ++j) {
					int8_t item = file.ReadInt8();
					if (j < Count2)
						pSeq->SetItem(j, item);
				}
			}
			if (inst.GetSequence(i) && inst.GetSequence(i)->GetItemCount() > 0)
				throw CModuleException::WithMessage("Document has no free sequence slot");
			inst.GetInstrumentManager()->SetSequence(inst.GetType(), i, inst.GetSeqIndex(i), pSeq);
		}
		catch (CModuleException &e) {
			e.AppendError("At " + std::string {inst.GetSequenceName(value_cast(i))} + " sequence,");
			throw e;
		}
	}
}



void CInstrumentIO2A03::DoWriteToModule(const CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<const CInstrument2A03 &>(inst_);
	CInstrumentIOSeq::DoWriteToModule(inst_, file);

	int Version = 6;
	int Octaves = Version >= 2 ? OCTAVE_RANGE : 6;

	if (Version >= 7)		// // // 050B
		file.WriteBlockInt(inst.GetSampleCount());
	for (int n = 0; n < NOTE_RANGE * Octaves; ++n) {
		if (Version >= 7) {		// // // 050B
			if (inst.GetSampleIndex(n) == CInstrument2A03::NO_DPCM)
				continue;
			file.WriteBlockChar(n);
		}
		file.WriteBlockChar(inst.GetSampleIndex(n) + 1);
		file.WriteBlockChar(inst.GetSamplePitch(n));
		if (Version >= 6)
			file.WriteBlockChar(inst.GetSampleDeltaValue(n));
	}
}

void CInstrumentIO2A03::ReadFromModule(CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<CInstrument2A03 &>(inst_);
	CInstrumentIOSeq::ReadFromModule(inst_, file);

	const int Version = file.GetBlockVersion();
	const int Octaves = (Version == 1) ? 6 : OCTAVE_RANGE;

	const auto ReadAssignment = [&] (int MidiNote) {
		try {
			int Index = AssertRange<MODULE_ERROR_STRICT>(
				file.GetBlockChar(), 0, MAX_DSAMPLES, "DPCM sample assignment index");
			if (Index > MAX_DSAMPLES)
				Index = 0;
			inst.SetSampleIndex(MidiNote, Index - 1);
			char Pitch = file.GetBlockChar();
			AssertRange<MODULE_ERROR_STRICT>(Pitch & 0x7F, 0, 0xF, "DPCM sample pitch");
			inst.SetSamplePitch(MidiNote, Pitch & 0x8F);
			if (Version > 5) {
				char Value = file.GetBlockChar();
				if (Value < -1) // not validated
					Value = -1;
				inst.SetSampleDeltaValue(MidiNote, Value);
			}
		}
		catch (CModuleException &e) {
			auto n = value_cast(ft0cc::doc::pitch_from_midi(MidiNote));
			auto o = ft0cc::doc::oct_from_midi(MidiNote);
			e.AppendError("At note " + conv::from_int(n) + ", octave " + conv::from_int(o) + ',');
			throw e;
		}
	};

	if (Version >= 7) {		// // // 050B
		const int Count = AssertRange<MODULE_ERROR_STRICT>(
			file.GetBlockInt(), 0, NOTE_COUNT, "DPCM sample assignment count");
		for (int i = 0; i < Count; ++i) {
			int Note = AssertRange<MODULE_ERROR_STRICT>(
				file.GetBlockChar(), 0, NOTE_COUNT - 1, "DPCM sample assignment note index");
			ReadAssignment(Note);
		}
	}
	else
		for (int n = 0; n < NOTE_RANGE * Octaves; ++n)
			ReadAssignment(n);
}

void CInstrumentIO2A03::DoWriteToFTI(const CInstrument &inst_, CSimpleFile &file) const {
	auto &inst = dynamic_cast<const CInstrument2A03 &>(inst_);
	CInstrumentIOSeq::DoWriteToFTI(inst_, file);

	// Saves an 2A03 instrument
	// Current version 2.4

	// DPCM
	const auto *pManager = inst.GetInstrumentManager();
	if (!pManager) {
		file.WriteInt32(0);
		file.WriteInt32(0);
		return;
	}

	unsigned int Count = inst.GetSampleCount();		// // // 050B
	file.WriteInt32(Count);

	bool UsedSamples[MAX_DSAMPLES] = { };

	int UsedCount = 0;
	for (int n = 0; n < NOTE_COUNT; ++n) {
		if (unsigned Sample = inst.GetSampleIndex(n); Sample != CInstrument2A03::NO_DPCM) {
			file.WriteInt8(n);
			file.WriteInt8(Sample + 1);
			file.WriteInt8(inst.GetSamplePitch(n));
			file.WriteInt8(inst.GetSampleDeltaValue(n));
			if (!UsedSamples[Sample])
				++UsedCount;
			UsedSamples[Sample] = true;
		}
	}

	// Write the number
	file.WriteInt32(UsedCount);

	// List of sample names
	for (int i = 0; i < MAX_DSAMPLES; ++i) if (UsedSamples[i]) {
		if (auto pSample = pManager->GetDSample(i)) {
			file.WriteInt32(i);
			file.WriteString(pSample->name());
			file.WriteString(std::string_view((const char *)pSample->data(), pSample->size()));
		}
	}
}

void CInstrumentIO2A03::DoReadFromFTI(CInstrument &inst_, CSimpleFile &file, int fti_ver) const {
	auto &inst = dynamic_cast<CInstrument2A03 &>(inst_);
	CInstrumentIOSeq::DoReadFromFTI(inst_, file, fti_ver);

	auto *pManager = inst.GetInstrumentManager();

	char SampleNames[MAX_DSAMPLES][ft0cc::doc::dpcm_sample::max_name_length + 1];

	unsigned int Count = file.ReadInt32();
	AssertRange(Count, 0U, static_cast<unsigned>(NOTE_COUNT), "DPCM assignment count");

	// DPCM instruments
	for (unsigned int i = 0; i < Count; ++i) {
		unsigned char InstNote = file.ReadInt8();
		try {
			unsigned char Sample = AssertRange(file.ReadInt8(), 0, 0x7F, "DPCM sample assignment index");
			if (Sample > MAX_DSAMPLES)
				Sample = 0;
			unsigned char Pitch = file.ReadInt8();
			AssertRange(Pitch & 0x7FU, 0U, 0xFU, "DPCM sample pitch");
			inst.SetSamplePitch(InstNote, Pitch);
			inst.SetSampleIndex(InstNote, Sample - 1);
			inst.SetSampleDeltaValue(InstNote, AssertRange(
				static_cast<char>(fti_ver >= 24 ? file.ReadInt8() : -1), -1, 0x7F, "DPCM sample delta value"));
		}
		catch (CModuleException &e) {
			auto n = value_cast(ft0cc::doc::pitch_from_midi(InstNote));
			auto o = ft0cc::doc::oct_from_midi(InstNote);
			e.AppendError("At note " + conv::from_int(n) + ", octave " + conv::from_int(o) + ',');
			throw e;
		}
	}

	// DPCM samples list
	bool bAssigned[NOTE_COUNT] = {};
	int TotalSize = 0;		// // // ???
	for (int i = 0; i < MAX_DSAMPLES; ++i)
		if (auto pSamp = pManager->GetDSample(i))
			TotalSize += pSamp->size();

	unsigned int SampleCount = file.ReadInt32();
	for (unsigned int i = 0; i < SampleCount; ++i) {
		int Index = AssertRange(
			file.ReadInt32(), 0, MAX_DSAMPLES - 1, "DPCM sample index");
		int Len = AssertRange(
			file.ReadInt32(), 0, (int)ft0cc::doc::dpcm_sample::max_name_length, "DPCM sample name length");
		file.ReadBytes(SampleNames[Index], Len);
		SampleNames[Index][Len] = '\0';
		int Size = file.ReadInt32();
		std::vector<uint8_t> SampleData(Size);
		file.ReadBytes(SampleData.data(), Size);
		auto pSample = std::make_shared<ft0cc::doc::dpcm_sample>(std::move(SampleData), SampleNames[Index]);

		bool Found = false;
		for (int j = 0; j < MAX_DSAMPLES; ++j) if (auto s = pManager->GetDSample(j)) {
			// Compare size and name to see if identical sample exists
			if (*pSample == *s) {
				Found = true;
				// Assign sample
				for (int n = 0; n < NOTE_COUNT; ++n)
					if (inst.GetSampleIndex(n) == Index && !bAssigned[n]) {
						inst.SetSampleIndex(n, j);
						bAssigned[n] = true;
					}
				break;
			}
		}
		if (Found)
			continue;

		// Load sample

		if (TotalSize + Size > MAX_SAMPLE_SPACE)
			throw CModuleException::WithMessage("Insufficient DPCM sample space (maximum " + conv::from_int(MAX_SAMPLE_SPACE / 1024) + " KB)");
		int FreeSample = pManager->AddDSample(pSample);
		if (FreeSample == -1)
			throw CModuleException::WithMessage("Document has no free DPCM sample slot");
		TotalSize += Size;
		// Assign it
		for (int n = 0; n < NOTE_COUNT; ++n)
			if (inst.GetSampleIndex(n) == Index && !bAssigned[n]) {
				inst.SetSampleIndex(n, FreeSample);
				bAssigned[n] = true;
			}
	}
}



void CInstrumentIOVRC7::DoWriteToModule(const CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<const CInstrumentVRC7 &>(inst_);

	file.WriteBlockInt(inst.GetPatch());

	for (int i = 0; i < 8; ++i)
		file.WriteBlockChar(inst.GetCustomReg(i));
}

void CInstrumentIOVRC7::ReadFromModule(CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<CInstrumentVRC7 &>(inst_);

	inst.SetPatch(AssertRange(file.GetBlockInt(), 0, 0xF, "OPLL patch number"));

	for (int i = 0; i < 8; ++i)
		inst.SetCustomReg(i, file.GetBlockChar());
}

void CInstrumentIOVRC7::DoWriteToFTI(const CInstrument &inst_, CSimpleFile &file) const {
	auto &inst = dynamic_cast<const CInstrumentVRC7 &>(inst_);

	file.WriteInt32(inst.GetPatch());

	for (int i = 0; i < 8; ++i)
		file.WriteInt8(inst.GetCustomReg(i));
}

void CInstrumentIOVRC7::DoReadFromFTI(CInstrument &inst_, CSimpleFile &file, int fti_ver) const {
	auto &inst = dynamic_cast<CInstrumentVRC7 &>(inst_);

	inst.SetPatch(file.ReadInt32());

	for (int i = 0; i < 8; ++i)
		inst.SetCustomReg(i, file.ReadInt8());
}



void CInstrumentIOFDS::DoWriteToModule(const CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<const CInstrumentFDS &>(inst_);

	// Write wave
	for (auto x : inst.GetSamples())
		file.WriteBlockChar(x);
	for (auto x : inst.GetModTable())
		file.WriteBlockChar(x);

	// Modulation parameters
	file.WriteBlockInt(inst.GetModulationSpeed());
	file.WriteBlockInt(inst.GetModulationDepth());
	file.WriteBlockInt(inst.GetModulationDelay());

	// Sequences
	const auto StoreSequence = [&] (const CSequence &Seq) {
		// Store number of items in this sequence
		file.WriteBlockChar(Seq.GetItemCount());
		// Store loop point
		file.WriteBlockInt(Seq.GetLoopPoint());
		// Store release point (v4)
		file.WriteBlockInt(Seq.GetReleasePoint());
		// Store setting (v4)
		file.WriteBlockInt(Seq.GetSetting());
		// Store items
		for (unsigned int j = 0; j < Seq.GetItemCount(); ++j) {
			file.WriteBlockChar(Seq.GetItem(j));
		}
	};
	StoreSequence(*inst.GetSequence(sequence_t::Volume));
	StoreSequence(*inst.GetSequence(sequence_t::Arpeggio));
	StoreSequence(*inst.GetSequence(sequence_t::Pitch));
}

void CInstrumentIOFDS::ReadFromModule(CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<CInstrumentFDS &>(inst_);

	unsigned char samples[64] = { };
	for (auto &x : samples)
		x = file.GetBlockChar();
	inst.SetSamples(samples);

	unsigned char modtable[32] = { };
	for (auto &x : modtable)
		x = file.GetBlockChar();
	inst.SetModTable(modtable);

	inst.SetModulationSpeed(file.GetBlockInt());
	inst.SetModulationDepth(file.GetBlockInt());
	inst.SetModulationDelay(file.GetBlockInt());

	// hack to fix earlier saved files (remove this eventually)
/*
	if (file.GetBlockVersion() > 2) {
		LoadSequence(pDocFile, GetSequence(sequence_t::Volume));
		LoadSequence(pDocFile, GetSequence(sequence_t::Arpeggio));
		if (file.GetBlockVersion() > 2)
			LoadSequence(pDocFile, GetSequence(sequence_t::Pitch));
	}
	else {
*/
	unsigned int a = file.GetBlockInt();
	unsigned int b = file.GetBlockInt();
	file.RollbackPointer(8);

	const auto LoadSequence = [&] (sequence_t SeqType) {
		int SeqCount = static_cast<unsigned char>(file.GetBlockChar());
		unsigned int LoopPoint = AssertRange(file.GetBlockInt(), -1, SeqCount - 1, "Sequence loop point");
		unsigned int ReleasePoint = AssertRange(file.GetBlockInt(), -1, SeqCount - 1, "Sequence release point");

		// AssertRange(SeqCount, 0, MAX_SEQUENCE_ITEMS, "Sequence item count", "%i");

		auto pSeq = std::make_shared<CSequence>(SeqType);
		pSeq->SetItemCount(SeqCount > MAX_SEQUENCE_ITEMS ? MAX_SEQUENCE_ITEMS : SeqCount);
		pSeq->SetLoopPoint(LoopPoint);
		pSeq->SetReleasePoint(ReleasePoint);
		pSeq->SetSetting(static_cast<seq_setting_t>(file.GetBlockInt()));

		for (int x = 0; x < SeqCount; ++x) {
			char Value = file.GetBlockChar();
			pSeq->SetItem(x, Value);
		}

		return pSeq;
	};

	if (a < 256 && (b & 0xFF) != 0x00) {
	}
	else {
		inst.SetSequence(sequence_t::Volume, LoadSequence(sequence_t::Volume));
		inst.SetSequence(sequence_t::Arpeggio, LoadSequence(sequence_t::Arpeggio));
		//
		// Note: Remove this line when files are unable to load
		// (if a file contains FDS instruments but FDS is disabled)
		// this was a problem in an earlier version.
		//
		if (file.GetBlockVersion() > 2)
			inst.SetSequence(sequence_t::Pitch, LoadSequence(sequence_t::Pitch));
	}

//	}

	// Older files was 0-15, new is 0-31
	if (file.GetBlockVersion() <= 3)
		DoubleVolume(*inst.GetSequence(sequence_t::Volume));
}

void CInstrumentIOFDS::DoWriteToFTI(const CInstrument &inst_, CSimpleFile &file) const {
	auto &inst = dynamic_cast<const CInstrumentFDS &>(inst_);

	// Write wave
	for (auto x : inst.GetSamples())
		file.WriteInt8(x);
	for (auto x : inst.GetModTable())
		file.WriteInt8(x);

	// Modulation parameters
	file.WriteInt32(inst.GetModulationSpeed());
	file.WriteInt32(inst.GetModulationDepth());
	file.WriteInt32(inst.GetModulationDelay());

	// Sequences
	const auto StoreInstSequence = [&] (const CSequence &Seq) {
		// Store number of items in this sequence
		file.WriteInt32(Seq.GetItemCount());
		// Store loop point
		file.WriteInt32(Seq.GetLoopPoint());
		// Store release point (v4)
		file.WriteInt32(Seq.GetReleasePoint());
		// Store setting (v4)
		file.WriteInt32(Seq.GetSetting());
		// Store items
		for (unsigned i = 0; i < Seq.GetItemCount(); ++i)
			file.WriteInt8(Seq.GetItem(i));
	};
	StoreInstSequence(*inst.GetSequence(sequence_t::Volume));
	StoreInstSequence(*inst.GetSequence(sequence_t::Arpeggio));
	StoreInstSequence(*inst.GetSequence(sequence_t::Pitch));
}

void CInstrumentIOFDS::DoReadFromFTI(CInstrument &inst_, CSimpleFile &file, int fti_ver) const {
	auto &inst = dynamic_cast<CInstrumentFDS &>(inst_);

	// Read wave
	unsigned char samples[64] = { };
	for (auto &x : samples)
		x = file.ReadInt8();
	inst.SetSamples(samples);

	unsigned char modtable[32] = { };
	for (auto &x : modtable)
		x = file.ReadInt8();
	inst.SetModTable(modtable);

	// Modulation parameters
	inst.SetModulationSpeed(file.ReadInt32());
	inst.SetModulationDepth(file.ReadInt32());
	inst.SetModulationDelay(file.ReadInt32());

	// Sequences
	const auto LoadInstSequence = [&] (sequence_t SeqType) {
		int SeqCount = AssertRange(file.ReadInt32(), 0, 0xFF, "Sequence item count");
		int Loop = AssertRange(static_cast<int>(file.ReadInt32()), -1, SeqCount - 1, "Sequence loop point");
		int Release = AssertRange(static_cast<int>(file.ReadInt32()), -1, SeqCount - 1, "Sequence release point");

		auto pSeq = std::make_shared<CSequence>(SeqType);
		pSeq->SetItemCount(SeqCount > MAX_SEQUENCE_ITEMS ? MAX_SEQUENCE_ITEMS : SeqCount);
		pSeq->SetLoopPoint(Loop);
		pSeq->SetReleasePoint(Release);
		pSeq->SetSetting(static_cast<seq_setting_t>(file.ReadInt32()));

		for (int i = 0; i < SeqCount; ++i)
			pSeq->SetItem(i, file.ReadInt8());

		return pSeq;
	};
	inst.SetSequence(sequence_t::Volume, LoadInstSequence(sequence_t::Volume));
	inst.SetSequence(sequence_t::Arpeggio, LoadInstSequence(sequence_t::Arpeggio));
	inst.SetSequence(sequence_t::Pitch, LoadInstSequence(sequence_t::Pitch));

	if (fti_ver <= 22)
		DoubleVolume(*inst.GetSequence(sequence_t::Volume));
}

void CInstrumentIOFDS::DoubleVolume(CSequence &seq) {
	for (unsigned int i = 0; i < seq.GetItemCount(); ++i)
		seq.SetItem(i, seq.GetItem(i) * 2);
}



void CInstrumentION163::DoWriteToModule(const CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<const CInstrumentN163 &>(inst_);
	CInstrumentIOSeq::DoWriteToModule(inst_, file);

	// Store wave
	file.WriteBlockInt(inst.GetWaveSize());
	file.WriteBlockInt(inst.GetWavePos());
	//file.WriteBlockInt(m_bAutoWavePos ? 1 : 0);
	file.WriteBlockInt(inst.GetWaveCount());

	for (int i = 0; i < inst.GetWaveCount(); ++i)
		for (auto x : inst.GetSamples(i))
			file.WriteBlockChar(x);
}

void CInstrumentION163::ReadFromModule(CInstrument &inst_, CDocumentFile &file) const {
	auto &inst = dynamic_cast<CInstrumentN163 &>(inst_);
	CInstrumentIOSeq::ReadFromModule(inst_, file);

	inst.SetWaveSize(AssertRange(file.GetBlockInt(), 4, CInstrumentN163::MAX_WAVE_SIZE, "N163 wave size"));
	inst.SetWavePos(AssertRange(file.GetBlockInt(), 0, CInstrumentN163::MAX_WAVE_SIZE - 1, "N163 wave position"));
	AssertRange<MODULE_ERROR_OFFICIAL>(inst.GetWavePos(), 0, 0x7F, "N163 wave position");
	if (file.GetBlockVersion() >= 8) {		// // // 050B
		(void)(file.GetBlockInt() != 0);
	}
	inst.SetWaveCount(AssertRange(file.GetBlockInt(), 1, CInstrumentN163::MAX_WAVE_COUNT, "N163 wave count"));
	AssertRange<MODULE_ERROR_OFFICIAL>(inst.GetWaveCount(), 1, 0x10, "N163 wave count");

	for (int i = 0; i < inst.GetWaveCount(); ++i) {
		for (unsigned j = 0; j < inst.GetWaveSize(); ++j) try {
			inst.SetSample(i, j, AssertRange(file.GetBlockChar(), 0, 15, "N163 wave sample"));
		}
		catch (CModuleException &e) {
			e.AppendError("At wave " + conv::from_int(i) + ", sample " + conv::from_int(j) + ',');
			throw e;
		}
	}
}

void CInstrumentION163::DoWriteToFTI(const CInstrument &inst_, CSimpleFile &file) const {
	auto &inst = dynamic_cast<const CInstrumentN163 &>(inst_);
	CInstrumentIOSeq::DoWriteToFTI(inst_, file);

	// Write wave config
	int WaveCount = inst.GetWaveCount();
	int WaveSize = inst.GetWaveSize();

	file.WriteInt32(WaveSize);
	file.WriteInt32(inst.GetWavePos());
//	file.WriteInt32(m_bAutoWavePos);		// // // 050B
	file.WriteInt32(WaveCount);

	for (int i = 0; i < WaveCount; ++i)
		for (auto x : inst.GetSamples(i))
			file.WriteInt8(x);
}

void CInstrumentION163::DoReadFromFTI(CInstrument &inst_, CSimpleFile &file, int fti_ver) const {
	auto &inst = dynamic_cast<CInstrumentN163 &>(inst_);
	CInstrumentIOSeq::DoReadFromFTI(inst_, file, fti_ver);

	// Read wave config
	int WaveSize = AssertRange(static_cast<int>(file.ReadInt32()), 4, CInstrumentN163::MAX_WAVE_SIZE, "N163 wave size");
	int WavePos = AssertRange(static_cast<int>(file.ReadInt32()), 0, CInstrumentN163::MAX_WAVE_SIZE - 1, "N163 wave position");
	if (fti_ver >= 25) {		// // // 050B
		bool AutoWavePos = file.ReadInt32() != 0;
		(void)AutoWavePos;
	}
	int WaveCount = AssertRange(static_cast<int>(file.ReadInt32()), 1, CInstrumentN163::MAX_WAVE_COUNT, "N163 wave count");

	inst.SetWaveSize(WaveSize);
	inst.SetWavePos(WavePos);
	inst.SetWaveCount(WaveCount);

	for (int i = 0; i < WaveCount; ++i)
		for (int j = 0; j < WaveSize; ++j) try {
			inst.SetSample(i, j, AssertRange(file.ReadInt8(), 0, 15, "N163 wave sample"));
		}
	catch (CModuleException &e) {
		e.AppendError("At wave " + conv::from_int(i) + ", sample " + conv::from_int(j) + ',');
		throw e;
	}
}
