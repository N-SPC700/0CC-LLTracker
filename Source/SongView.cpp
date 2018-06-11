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

#include "SongView.h"
#include "SongData.h"
#include "TrackData.h"
#include "ft0cc/doc/pattern_note.hpp"

CConstSongView::CConstSongView(const CChannelOrder &order, const CSongData &song, bool showSkippedRows) :
	order_(order), song_(song), show_skipped_(showSkippedRows)
{
}

CSongView::CSongView(const CChannelOrder &order, CSongData &song, bool showSkippedRows) :
	CConstSongView(order, const_cast<CSongData &>(song), showSkippedRows)
{
}

CChannelOrder &CConstSongView::GetChannelOrder() {
	return order_;
}

const CChannelOrder &CConstSongView::GetChannelOrder() const {
	return order_;
}

CSongData &CSongView::GetSong() {
	return const_cast<CSongData &>(song_);
}

const CSongData &CConstSongView::GetSong() const {
	return song_;
}

CTrackData *CSongView::GetTrack(std::size_t index) {
	return GetSong().GetTrack(order_.TranslateChannel(index));
}

const CTrackData *CConstSongView::GetTrack(std::size_t index) const {
	return GetSong().GetTrack(order_.TranslateChannel(index));
}

CPatternData &CSongView::GetPattern(std::size_t index, unsigned Pattern) {
	return const_cast<CPatternData &>(CConstSongView::GetPattern(index, Pattern));
}

const CPatternData &CConstSongView::GetPattern(std::size_t index, unsigned Pattern) const {
	auto pTrack = GetTrack(index);		// // //
	if (!pTrack)
		throw std::out_of_range {"Bad track index in CConstSongView::GetPattern(std::size_t, unsigned)"};
	return pTrack->GetPattern(Pattern);
}

CPatternData &CSongView::GetPatternOnFrame(std::size_t index, unsigned Frame) {
	return GetPattern(index, GetFramePattern(index, Frame));
}

const CPatternData &CConstSongView::GetPatternOnFrame(std::size_t index, unsigned Frame) const {
	return GetPattern(index, GetFramePattern(index, Frame));
}

unsigned int CConstSongView::GetFramePattern(std::size_t index, unsigned Frame) const {
	auto pTrack = GetTrack(index);
	return pTrack ? pTrack->GetFramePattern(Frame) : MAX_PATTERN;
}

void CSongView::SetFramePattern(std::size_t index, unsigned Frame, unsigned Pattern) {
	if (auto pTrack = GetTrack(index))
		pTrack->SetFramePattern(Frame, Pattern);
}

unsigned CConstSongView::GetEffectColumnCount(std::size_t index) const {
	auto pTrack = GetTrack(index);
	return pTrack ? pTrack->GetEffectColumnCount() : 0;
}

void CSongView::SetEffectColumnCount(std::size_t index, unsigned Count) {
	if (auto pTrack = GetTrack(index))
		pTrack->SetEffectColumnCount(Count);
}

unsigned CConstSongView::GetFrameLength(unsigned Frame) const {
	if (show_skipped_)		// // //
		return GetSong().GetPatternLength();

	const unsigned PatternLength = GetSong().GetPatternLength();	// default length
	unsigned HaltPoint = PatternLength;

	const auto trackLen = [Frame, PatternLength] (const CTrackData &track) {
		const unsigned Columns = track.GetEffectColumnCount();
		const auto &pat = track.GetPatternOnFrame(Frame);
		for (unsigned j = 0; j < PatternLength - 1; ++j) {
			const auto &Note = pat.GetNoteOn(j);
			for (unsigned k = 0; k < Columns; ++k)
				switch (Note.fx_name(k))
				case ft0cc::doc::effect_type::SKIP: case ft0cc::doc::effect_type::JUMP: case ft0cc::doc::effect_type::HALT:
					return j + 1;
		}
		return PatternLength;
	};

	ForeachTrack([&] (const CTrackData &track) {
		HaltPoint = std::min(HaltPoint, trackLen(track));
	});

	return HaltPoint;
}

void CSongView::PullUp(std::size_t index, unsigned Frame, unsigned Row) {
	GetSong().PullUp(order_.TranslateChannel(index), Frame, Row);
}

void CSongView::InsertRow(std::size_t index, unsigned Frame, unsigned Row) {
	GetSong().InsertRow(order_.TranslateChannel(index), Frame, Row);
}
