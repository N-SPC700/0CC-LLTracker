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

#define _SCL_SECURE_NO_WARNINGS
#include "DocumentFile.h"
#include "SimpleFile.h"
#include "ModuleException.h"
#include "array_view.h"
#include "NumConv.h"
#include <cstring>		// // //
#include "Assertion.h"		// // //

//
// This class is based on CSimpleFile and has some simple extensions to create and read FTM files
//

// Class constants
const unsigned int CDocumentFile::FILE_VER		 = 0x0440;			// Current file version (4.40)
const unsigned int CDocumentFile::COMPATIBLE_VER = 0x0100;			// Compatible file version (1.0)

//const std::string_view CDocumentFile::FILE_HEADER_ID = {"LLTracker Module", 18};		// // //
//const std::string_view CDocumentFile::FILE_END_ID = "END";

const unsigned int CDocumentFile::MAX_BLOCK_SIZE = 0x80000;
const unsigned int CDocumentFile::BLOCK_SIZE = 0x10000;

CDocumentFile::CDocumentFile() :
	m_pFile(std::make_unique<CSimpleFile>())
{
}

CDocumentFile::~CDocumentFile() {
	Close();
}

// // // delegations to CSimpleFile

CSimpleFile &CDocumentFile::GetCSimpleFile() {
	return *m_pFile;
}

void CDocumentFile::Open(const fs::path &fname, std::ios::openmode nOpenFlags) {		// // //
	m_pFile->Open(fname, nOpenFlags);
}

void CDocumentFile::Close() {
	m_pFile->Close();
}

// CDocumentFile

bool CDocumentFile::Finished() const
{
	return m_bFileDone;
}

void CDocumentFile::BeginDocument()		// // //
{
	Write(reinterpret_cast<const unsigned char *>(FILE_HEADER_ID.data()), FILE_HEADER_ID.size());		// // //
	Write(reinterpret_cast<const unsigned char *>(&FILE_VER), sizeof(FILE_VER));
}

void CDocumentFile::EndDocument()
{
	Write(reinterpret_cast<const unsigned char *>(FILE_END_ID.data()), FILE_END_ID.size());		// // //
}

void CDocumentFile::CreateBlock(std::string_view ID, int Version)		// // //
{
	Assert(ID.size() < BLOCK_HEADER_SIZE);		// // //
	m_cBlockID.fill(0);		// // //
	ID = ID.substr(0, std::size(m_cBlockID) - 1);
	ID.copy(m_cBlockID.data(), ID.size());

	m_iBlockPointer = 0;
	m_iBlockSize	= 0;
	m_iBlockVersion = Version & 0xFFFF;

	m_iMaxBlockSize = BLOCK_SIZE;

	m_pBlockData = std::vector<unsigned char>(m_iMaxBlockSize);		// // //
}

void CDocumentFile::ReallocateBlock()
{
	m_iMaxBlockSize += BLOCK_SIZE;
	m_pBlockData.resize(m_iMaxBlockSize);		// // //
}

void CDocumentFile::WriteBlock(array_view<unsigned char> Data)		// // //
{
	Assert(!m_pBlockData.empty());		// // //

	// Allow block to grow in size

	unsigned Previous = m_iBlockPointer;
	while (!Data.empty()) {
		auto Block = Data.subview(0, BLOCK_SIZE);

		if ((m_iBlockPointer + Block.size()) >= m_iMaxBlockSize)
			ReallocateBlock();

		Block.copy(m_pBlockData.data() + m_iBlockPointer, Block.size());		// // //
		m_iBlockPointer += Block.size();
		Data.remove_front(Block.size());
	}
	m_iPreviousPointer = Previous;
}

template <typename T>
void CDocumentFile::WriteBlockData(T Value)
{
	auto ptr = reinterpret_cast<const unsigned char *>(&Value);
	WriteBlock({ptr, sizeof(Value)});
}

void CDocumentFile::WriteBlockInt(int Value)
{
	WriteBlockData(Value);
}

void CDocumentFile::WriteBlockChar(char Value)
{
	WriteBlockData(Value);
}

void CDocumentFile::WriteString(std::string_view sv) {		// // //
	WriteBlock({(const unsigned char *)sv.data(), sv.size()});
	WriteBlockChar(0);
}

void CDocumentFile::WriteStringPadded(std::string_view sv, std::size_t n) {
	sv = sv.substr(0, n - 1);
	WriteBlock({(const unsigned char *)sv.data(), sv.size()});
	for (std::size_t i = sv.size(); i < n; ++i)
		WriteBlockChar(0);
}

void CDocumentFile::WriteStringCounted(std::string_view sv) {
	WriteBlockInt(sv.size());
	WriteBlock({(const unsigned char *)sv.data(), sv.size()});
}

bool CDocumentFile::FlushBlock()
{
	if (m_pBlockData.empty())
		return false;

	if (m_iBlockPointer) {		// // //
		Write(reinterpret_cast<unsigned char *>(m_cBlockID.data()), std::size(m_cBlockID) * sizeof(char));
		Write(reinterpret_cast<unsigned char *>(&m_iBlockVersion), sizeof(m_iBlockVersion));
		Write(reinterpret_cast<unsigned char *>(&m_iBlockPointer), sizeof(m_iBlockPointer));
		Write(m_pBlockData.data(), m_iBlockPointer);		// // //
	}

	m_pBlockData.clear();		// // //

	return true;
}

void CDocumentFile::ValidateFile()
{
	// Checks if loaded file is valid

	// Check ident string
	char Buffer[FILE_HEADER_ID.size()] = { };
	Read(reinterpret_cast<unsigned char *>(Buffer), FILE_HEADER_ID.size());		// // //

	if (array_view<char> {Buffer} != FILE_HEADER_ID)
		RaiseModuleException("File is not a FamiTracker module");

	// Read file version
	unsigned char VerBuffer[4] = { };		// // //
	Read(VerBuffer, std::size(VerBuffer));
	m_iFileVersion = (VerBuffer[3] << 24) | (VerBuffer[2] << 16) | (VerBuffer[1] << 8) | VerBuffer[0];

	// // // Older file version
	if (GetFileVersion() < COMPATIBLE_VER)
		throw CModuleException::WithMessage("LLTracker module version too old (0x" + conv::from_int_hex(GetFileVersion()) +
			"), expected 0x" + conv::from_int_hex(COMPATIBLE_VER) + " or above");

	// // // File version is too new
	if (GetFileVersion() > 0x450u /*FILE_VER*/)		// // // 050B
		throw CModuleException::WithMessage("LLTracker module version too new (0x" + conv::from_int_hex(GetFileVersion()) +
			"), expected 0x" + conv::from_int_hex(0x450u) + " or below");

	m_bFileDone = false;
	m_bIncomplete = false;
}

unsigned int CDocumentFile::GetFileVersion() const
{
	return m_iFileVersion & 0xFFFF;
}

bool CDocumentFile::ReadBlock()
{
	m_iBlockPointer = 0;

	m_cBlockID.fill(0);		// // //

	int BytesRead = Read(reinterpret_cast<unsigned char *>(m_cBlockID.data()), std::size(m_cBlockID) * sizeof(char));
	Read(reinterpret_cast<unsigned char *>(&m_iBlockVersion), sizeof(m_iBlockVersion));
	Read(reinterpret_cast<unsigned char *>(&m_iBlockSize), sizeof(m_iBlockSize));

	if (m_iBlockSize > 50000000) {
		// File is probably corrupt
		m_cBlockID.fill(0);		// // //
		return true;
	}

	m_pBlockData = std::vector<unsigned char>(m_iBlockSize);		// // //
	if (Read(m_pBlockData.data(), m_iBlockSize) == FILE_END_ID.size())		// // //
		if (array_view<char> {m_cBlockID.data(), FILE_END_ID.size()} == FILE_END_ID)
			m_bFileDone = true;

	if (BytesRead == 0)
		m_bFileDone = true;
/*
	if (GetPosition() == GetLength() && !m_bFileDone) {
		// Parts of file is missing
		m_bIncomplete = true;
		m_cBlockID.fill(0);
		return true;
	}
*/
	return false;
}

const char *CDocumentFile::GetBlockHeaderID() const		// // //
{
	return m_cBlockID.data();
}

int CDocumentFile::GetBlockVersion() const
{
	return m_iBlockVersion;
}

void CDocumentFile::RollbackPointer(int count)
{
	m_iBlockPointer -= count;
	m_iPreviousPointer = m_iBlockPointer; // ?
	m_iFilePosition -= count;		// // //
	m_iPreviousPosition -= count;
}

int CDocumentFile::GetBlockInt()
{
	int Value;
	std::memcpy(&Value, m_pBlockData.data() + m_iBlockPointer, sizeof(Value));		// // //
	m_iPreviousPointer = m_iBlockPointer;
	m_iBlockPointer += sizeof(Value);
	m_iPreviousPosition = m_iFilePosition;		// // //
	m_iFilePosition += sizeof(Value);
	return Value;
}

char CDocumentFile::GetBlockChar()
{
	char Value;
	std::memcpy(&Value, m_pBlockData.data() + m_iBlockPointer, sizeof(Value));		// // //
	m_iPreviousPointer = m_iBlockPointer;
	m_iBlockPointer += sizeof(Value);
	m_iPreviousPosition = m_iFilePosition;		// // //
	m_iFilePosition += sizeof(Value);
	return Value;
}

std::string CDocumentFile::ReadString()
{
	/*
	char str[1024], c;
	int str_ptr = 0;

	while ((c = GetBlockChar()) && (str_ptr < 1023))
		str[str_ptr++] = c;

	str[str_ptr++] = 0;

	return CStringW(str);
	*/

	std::string str;
	int str_ptr = 0;

	unsigned int Previous = m_iBlockPointer;
	while (str_ptr++ < 65536)
		if (char c = GetBlockChar())
			str.push_back(c);
		else
			break;
	m_iPreviousPointer = Previous;

	return str;
}

void CDocumentFile::GetBlock(void *Buffer, int Size)
{
	Assert(Size < MAX_BLOCK_SIZE);
	Assert(Buffer != NULL);

	std::memcpy(Buffer, m_pBlockData.data() + m_iBlockPointer, Size);		// // //
	m_iPreviousPointer = m_iBlockPointer;
	m_iBlockPointer += Size;
	m_iPreviousPosition = m_iFilePosition;		// // //
	m_iFilePosition += Size;
}

bool CDocumentFile::BlockDone() const
{
	return (m_iBlockPointer >= m_iBlockSize);
}

int CDocumentFile::GetBlockPos() const
{
	return m_iBlockPointer;
}

int CDocumentFile::GetBlockSize() const
{
	return m_iBlockSize;
}

bool CDocumentFile::IsFileIncomplete() const
{
	return m_bIncomplete;
}

CModuleException CDocumentFile::GetException() const		// // //
{
	CModuleException e;
	SetDefaultFooter(e);
	return e;
}

void CDocumentFile::SetDefaultFooter(CModuleException &e) const		// // //
{
	std::string msg = "At address 0x" + conv::from_int_hex(m_iPreviousPointer) +
		" in " + std::string(m_cBlockID.data(), m_cBlockID.size()) + " block,\naddress 0x" +
		conv::from_int_hex(m_iPreviousPosition) + " in file";		// // //
	e.SetFooter(std::move(msg));
}

void CDocumentFile::RaiseModuleException(const std::string &Msg) const		// // //
{
	CModuleException e = GetException();
	e.AppendError(Msg);
	throw e;
}

unsigned CDocumentFile::Read(unsigned char *lpBuf, std::size_t nCount)		// // //
{
	m_iPreviousPosition = m_iFilePosition;
	m_iFilePosition = m_pFile->GetPosition();
	return m_pFile->ReadBytes(lpBuf, nCount);
}

void CDocumentFile::Write(const unsigned char *lpBuf, std::size_t nCount)		// // //
{
	m_iPreviousPosition = m_iFilePosition;
	m_iFilePosition = m_pFile->GetPosition();
	m_pFile->WriteBytes({lpBuf, nCount});
}
