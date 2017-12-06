/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** 0CC-FamiTracker is (C) 2014-2017 HertzDevil
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

#include <vector>
#include <memory>		// // //
#include "stdafx.h"

//
// Text chunk renderer
//

class CChunkRenderText;
class CChunk;		// // //
class CDSample;		// // //
enum chunk_type_t : int;		// // //
struct stChunkLabel;		// // //

typedef void (CChunkRenderText::*renderFunc_t)(CChunk *pChunk, CFile *pFile);

struct stChunkRenderFunc {
	chunk_type_t type;
	renderFunc_t function;
};

class CChunkRenderText
{
public:
	CChunkRenderText(CFile *pFile);
	void StoreChunks(const std::vector<std::shared_ptr<CChunk>> &Chunks);		// // //
	void StoreSamples(const std::vector<const CDSample*> &Samples);

private:
	static const stChunkRenderFunc RENDER_FUNCTIONS[];
	static CStringA GetLabelString(const stChunkLabel &label);		// // //

private:
	void DumpStrings(const CStringA &preStr, const CStringA &postStr, CStringArray &stringArray, CFile *pFile) const;
	void WriteFileString(const CStringA &str, CFile *pFile) const;
	void StoreByteString(const char *pData, int Len, CStringA &str, int LineBreak) const;
	void StoreByteString(const CChunk *pChunk, CStringA &str, int LineBreak) const;

private:
	void StoreHeaderChunk(CChunk *pChunk, CFile *pFile);
	void StoreInstrumentListChunk(CChunk *pChunk, CFile *pFile);
	void StoreInstrumentChunk(CChunk *pChunk, CFile *pFile);
	void StoreSequenceChunk(CChunk *pChunk, CFile *pFile);
	void StoreSampleListChunk(CChunk *pChunk, CFile *pFile);
	void StoreSamplePointersChunk(CChunk *pChunk, CFile *pFile);
	void StoreGrooveListChunk(CChunk *pChunk, CFile *pFile);		// // //
	void StoreGrooveChunk(CChunk *pChunk, CFile *pFile);		// // //
	void StoreSongListChunk(CChunk *pChunk, CFile *pFile);
	void StoreSongChunk(CChunk *pChunk, CFile *pFile);
	void StoreFrameListChunk(CChunk *pChunk, CFile *pFile);
	void StoreFrameChunk(CChunk *pChunk, CFile *pFile);
	void StorePatternChunk(CChunk *pChunk, CFile *pFile);
	void StoreWavetableChunk(CChunk *pChunk, CFile *pFile);
	void StoreWavesChunk(CChunk *pChunk, CFile *pFile);

private:
	CStringArray m_headerStrings;
	CStringArray m_instrumentListStrings;
	CStringArray m_instrumentStrings;
	CStringArray m_sequenceStrings;
	CStringArray m_sampleListStrings;
	CStringArray m_samplePointersStrings;
	CStringArray m_grooveListStrings;		// // //
	CStringArray m_grooveStrings;		// // //
	CStringArray m_songListStrings;
	CStringArray m_songStrings;
	CStringArray m_songDataStrings;
	CStringArray m_wavetableStrings;
	CStringArray m_wavesStrings;

	CFile *m_pFile;
};
