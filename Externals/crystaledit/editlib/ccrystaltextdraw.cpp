////////////////////////////////////////////////////////////////////////////
//  File:       ccrystaltextview.cpp
//  Version:    1.2.0.5
//  Created:    29-Dec-1998
//
//  Author:     Stcherbatchenko Andrei
//  E-mail:     windfall@gmx.de
//
//  Implementation of the CCrystalTextView class, a part of Crystal Edit -
//  syntax coloring text editor.
//
//  You are free to use or modify this code to the following restrictions:
//  - Acknowledge me somewhere in your about box, simple "Parts of code by.."
//  will be enough. If you can't (or don't want to), contact me personally.
//  - LEAVE THIS HEADER INTACT
////////////////////////////////////////////////////////////////////////////

/**
 * @file  ccrystaltextview.cpp
 *
 * @brief Implementation of the CCrystalTextView class
 */
 // ID line follows -- this is updated by SVN
 // $Id: ccrystaltextview.cpp 7117 2010-02-01 14:24:51Z sdottaka $

#include "StdAfx.h"
#include <vector>
#include <malloc.h>
#include <imm.h> /* IME */
#include <mbctype.h>
#include "editcmd.h"
#include "editreg.h"
#include "ccrystaltextview.h"
#include "ccrystaltextbuffer.h"
#include "cfindtextdlg.h"
#include "fpattern.h"
#include "filesup.h"
#include "registry.h"
#include "gotodlg.h"
#include "ViewableWhitespace.h"
#include "SyntaxColors.h"
#include "string_util.h"

using std::vector;

// Escaped character constants in range 0x80-0xFF are interpreted in current codepage
// Using C locale gets us direct mapping to Unicode codepoints
#pragma setlocale("C")

#ifndef __AFXPRIV_H__
#pragma message("Include <afxpriv.h> in your stdafx.h to avoid this message")
#include <afxpriv.h>
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#endif



#pragma warning ( disable : 4100 )
////////////////////////////////////////////////////////////////////////////
// CCrystalTextView

//IMPLEMENT_DYNCREATE(CCrystalTextView, CView)

class IntArray : public CArray<int, int>
{
public:
	explicit IntArray(int len) { SetSize(len); }
};

int CCrystalTextView::
MergeTextBlocks(TEXTBLOCK *pBuf1, int nBlocks1, TEXTBLOCK *pBuf2,
	int nBlocks2, TEXTBLOCK *&pMergedBuf)
{
	int i, j, k;

	pMergedBuf = new TEXTBLOCK[nBlocks1 + nBlocks2];

	for (i = 0, j = 0, k = 0; ; k++)
	{
		if (i >= nBlocks1 && j >= nBlocks2)
		{
			break;
		}
		else if ((i < nBlocks1 && j < nBlocks2) &&
			(pBuf1[i].m_nCharPos == pBuf2[j].m_nCharPos))
		{
			pMergedBuf[k].m_nCharPos = pBuf2[j].m_nCharPos;
			if (pBuf2[j].m_nColorIndex == COLORINDEX_NONE)
				pMergedBuf[k].m_nColorIndex = pBuf1[i].m_nColorIndex;
			else
				pMergedBuf[k].m_nColorIndex = pBuf2[j].m_nColorIndex;
			if (pBuf2[j].m_nBgColorIndex == COLORINDEX_NONE)
				pMergedBuf[k].m_nBgColorIndex = pBuf1[i].m_nBgColorIndex;
			else
				pMergedBuf[k].m_nBgColorIndex = pBuf2[j].m_nBgColorIndex;
			i++;
			j++;
		}
		else if (j >= nBlocks2 || (i < nBlocks1 &&
			pBuf1[i].m_nCharPos < pBuf2[j].m_nCharPos))
		{
			pMergedBuf[k].m_nCharPos = pBuf1[i].m_nCharPos;
			if (nBlocks2 == 0 || pBuf2[j - 1].m_nColorIndex == COLORINDEX_NONE)
				pMergedBuf[k].m_nColorIndex = pBuf1[i].m_nColorIndex;
			else
				pMergedBuf[k].m_nColorIndex = pBuf2[j - 1].m_nColorIndex;
			if (nBlocks2 == 0 || pBuf2[j - 1].m_nBgColorIndex == COLORINDEX_NONE)
				pMergedBuf[k].m_nBgColorIndex = pBuf1[i].m_nBgColorIndex;
			else
				pMergedBuf[k].m_nBgColorIndex = pBuf2[j - 1].m_nBgColorIndex;
			i++;
		}
		else if (i >= nBlocks1 || (j < nBlocks2 && pBuf1[i].m_nCharPos > pBuf2[j].m_nCharPos))
		{
			pMergedBuf[k].m_nCharPos = pBuf2[j].m_nCharPos;
			if (i > 0 && pBuf2[j].m_nColorIndex == COLORINDEX_NONE)
				pMergedBuf[k].m_nColorIndex = pBuf1[i - 1].m_nColorIndex;
			else
				pMergedBuf[k].m_nColorIndex = pBuf2[j].m_nColorIndex;
			if (i > 0 && pBuf2[j].m_nBgColorIndex == COLORINDEX_NONE)
				pMergedBuf[k].m_nBgColorIndex = pBuf1[i - 1].m_nBgColorIndex;
			else
				pMergedBuf[k].m_nBgColorIndex = pBuf2[j].m_nBgColorIndex;
			j++;
		}
	}

	j = 0;
	for (i = 0; i < k; ++i)
	{
		if (i == 0 ||
			(pMergedBuf[i - 1].m_nColorIndex != pMergedBuf[i].m_nColorIndex ||
				pMergedBuf[i - 1].m_nBgColorIndex != pMergedBuf[i].m_nBgColorIndex))
		{
			pMergedBuf[j] = pMergedBuf[i];
			++j;
		}
	}

	return j;
}

/**
 * @brief Draw a chunk of text (one color, one line, full or part of line)
 *
 * @note In ANSI build, this routine is buggy for multibytes or double-width characters
 */
void CCrystalTextView::
DrawLineHelperImpl(CDC * pdc, CPoint & ptOrigin, const CRect & rcClip,
	int nColorIndex, int nBgColorIndex, COLORREF crText, COLORREF crBkgnd, LPCTSTR pszChars, int nOffset, int nCount, int &nActualOffset)
{
	ASSERT(nCount >= 0);
	if (nCount > 0)
	{
		CString line;
		nActualOffset += ExpandChars(pszChars, nOffset, nCount, line, nActualOffset);
		const int lineLen = line.GetLength();
		const int nCharWidth = GetCharWidth();
		const int nCharWidthNarrowed = nCharWidth / 2;
		const int nCharWidthWidened = nCharWidth * 2 - nCharWidthNarrowed;
		const int nLineHeight = GetLineHeight();

		// i the character index, from 0 to lineLen-1
		int i = 0;

		// Pass if the text begins after the right end of the clipping region
		if (ptOrigin.x < rcClip.right)
		{
			// Because ExtTextOut is buggy when ptOrigin.x < - 4095 * charWidth
			// or when nCount >= 4095
			// and because this is not well documented,
			// we decide to do the left & right clipping here

			// Update the position after the left clipped characters
			// stop for i = first visible character, at least partly
			const int clipLeft = rcClip.left - nCharWidth * 2;
			for (; i < lineLen; i++)
			{
				int pnWidthsCurrent = GetCharWidthFromChar(line[i]);
#ifndef _UNICODE
				if (_ismbblead(line[i]))
					pnWidthsCurrent *= 2;
#endif
				ptOrigin.x += pnWidthsCurrent;
				if (ptOrigin.x >= clipLeft)
				{
					ptOrigin.x -= pnWidthsCurrent;
					break;
				}
#ifndef _UNICODE
				if (_ismbblead(line[i]))
					i++;
#endif
			}

			// 
#ifdef _DEBUG
		  //CSize sz = pdc->GetTextExtent(line, nCount);
		  //ASSERT(sz.cx == m_nCharWidth * nCount);
#endif

			if (i < lineLen)
			{
				// We have to draw some characters
				int ibegin = i;
				int nSumWidth = 0;

				// A raw estimate of the number of characters to display
				// For wide characters, nCountFit may be overvalued
				int nWidth = rcClip.right - ptOrigin.x;
				int nCount = lineLen - ibegin;
				int nCountFit = nWidth / nCharWidth + 2/* wide char */;
				if (nCount > nCountFit) {
#ifndef _UNICODE
					if (_ismbslead((unsigned char *)(LPCSTR)line, (unsigned char *)(LPCSTR)line + nCountFit - 1))
						nCountFit++;
#endif
					nCount = nCountFit;
				}

				// Table of charwidths as CCrystalEditor thinks they are
				// Seems that CrystalEditor's and ExtTextOut()'s charwidths aren't
				// same with some fonts and text is drawn only partially
				// if this table is not used.
				vector<int> nWidths(nCount + 2);
				for (; i < nCount + ibegin; i++)
				{
					if (line[i] == '\t') // Escape sequence leadin?
					{
						// Substitute a space narrowed to half the width of a character cell.
						line.SetAt(i, ' ');
						nSumWidth += nWidths[i - ibegin] = nCharWidthNarrowed;
						// 1st hex digit has normal width.
						nSumWidth += nWidths[++i - ibegin] = nCharWidth;
						// 2nd hex digit is padded by half the width of a character cell.
						nSumWidth += nWidths[++i - ibegin] = nCharWidthWidened;
					}
					else
					{
						nSumWidth += nWidths[i - ibegin] = GetCharWidthFromChar(line[i]);
					}
				}

				if (ptOrigin.x + nSumWidth > rcClip.left)
				{
					if (crText == CLR_NONE || nColorIndex & COLORINDEX_APPLYFORCE)
						pdc->SetTextColor(GetColor(nColorIndex));
					else
						pdc->SetTextColor(crText);
					if (crBkgnd == CLR_NONE || nBgColorIndex & COLORINDEX_APPLYFORCE)
						pdc->SetBkColor(GetColor(nBgColorIndex));
					else
						pdc->SetBkColor(crBkgnd);

					pdc->SelectObject(GetFont(GetItalic(nColorIndex),
						GetBold(nColorIndex)));
					// we are sure to have less than 4095 characters because all the chars are visible
					RECT rcIntersect;
					RECT rcTextBlock = { ptOrigin.x, ptOrigin.y, ptOrigin.x + nSumWidth + 2, ptOrigin.y + nLineHeight };
					IntersectRect(&rcIntersect, &rcClip, &rcTextBlock);
					VERIFY(pdc->ExtTextOut(ptOrigin.x, ptOrigin.y, ETO_CLIPPED | ETO_OPAQUE,
						&rcIntersect, LPCTSTR(line) + ibegin, nCount, &nWidths[0]));
					// Draw rounded rectangles around control characters
					pdc->SaveDC();
					pdc->IntersectClipRect(&rcClip);
					HDC hDC = pdc->m_hDC;
					HGDIOBJ hBrush = ::GetStockObject(NULL_BRUSH);
					hBrush = ::SelectObject(hDC, hBrush);
					HGDIOBJ hPen = ::CreatePen(PS_SOLID, 1, ::GetTextColor(hDC));
					hPen = ::SelectObject(hDC, hPen);
					int x = ptOrigin.x;
					for (int j = 0; j < nCount; ++j)
					{
						// Assume narrowed space is converted escape sequence leadin.
						if (line[ibegin + j] == ' ' && nWidths[j] < nCharWidth)
						{
							::RoundRect(hDC, x + 2, ptOrigin.y + 1,
								x + 3 * nCharWidth - 2, ptOrigin.y + nLineHeight - 1,
								nCharWidth / 2, nLineHeight / 2);
						}
						x += nWidths[j];
					}
					hPen = ::SelectObject(hDC, hPen);
					::DeleteObject(hPen);
					hBrush = ::SelectObject(hDC, hBrush);
					pdc->RestoreDC(-1);
				}

				// Update the final position after the visible characters	              
				ptOrigin.x += nSumWidth;

			}
		}
		// Update the final position after the right clipped characters
		for (; i < lineLen; i++)
		{
			ptOrigin.x += GetCharWidthFromChar(line[i]);
		}
	}
}

void CCrystalTextView::
DrawLineHelper(CDC * pdc, CPoint & ptOrigin, const CRect & rcClip, int nColorIndex, int nBgColorIndex,
	COLORREF crText, COLORREF crBkgnd, LPCTSTR pszChars, int nOffset, int nCount, int &nActualOffset, CPoint ptTextPos)
{
	if (nCount > 0)
	{
		if (m_bFocused || m_bShowInactiveSelection)
		{
			int nSelBegin = 0, nSelEnd = 0;
			if (!m_bColumnSelection)
			{
				if (m_ptDrawSelStart.y > ptTextPos.y)
				{
					nSelBegin = nCount;
				}
				else if (m_ptDrawSelStart.y == ptTextPos.y)
				{
					nSelBegin = m_ptDrawSelStart.x - ptTextPos.x;
					if (nSelBegin < 0)
						nSelBegin = 0;
					if (nSelBegin > nCount)
						nSelBegin = nCount;
				}
				if (m_ptDrawSelEnd.y > ptTextPos.y)
				{
					nSelEnd = nCount;
				}
				else if (m_ptDrawSelEnd.y == ptTextPos.y)
				{
					nSelEnd = m_ptDrawSelEnd.x - ptTextPos.x;
					if (nSelEnd < 0)
						nSelEnd = 0;
					if (nSelEnd > nCount)
						nSelEnd = nCount;
				}
			}
			else
			{
				int nSelLeft, nSelRight;
				GetColumnSelection(ptTextPos.y, nSelLeft, nSelRight);
				nSelBegin = nSelLeft - ptTextPos.x;
				nSelEnd = nSelRight - ptTextPos.x;
				if (nSelBegin < 0) nSelBegin = 0;
				if (nSelBegin > nCount) nSelBegin = nCount;
				if (nSelEnd < 0) nSelEnd = 0;
				if (nSelEnd > nCount) nSelEnd = nCount;
			}

			ASSERT(nSelBegin >= 0 && nSelBegin <= nCount);
			ASSERT(nSelEnd >= 0 && nSelEnd <= nCount);
			ASSERT(nSelBegin <= nSelEnd);

			//  Draw part of the text before selection
			if (nSelBegin > 0)
			{
				DrawLineHelperImpl(pdc, ptOrigin, rcClip, nColorIndex, nBgColorIndex, crText, crBkgnd, pszChars, nOffset, nSelBegin, nActualOffset);
			}
			if (nSelBegin < nSelEnd)
			{
				DrawLineHelperImpl(pdc, ptOrigin, rcClip,
					nColorIndex & ~COLORINDEX_APPLYFORCE, nBgColorIndex & ~COLORINDEX_APPLYFORCE,
					GetColor(COLORINDEX_SELTEXT),
					GetColor(COLORINDEX_SELBKGND),
					pszChars, nOffset + nSelBegin, nSelEnd - nSelBegin, nActualOffset);
			}
			if (nSelEnd < nCount)
			{
				DrawLineHelperImpl(pdc, ptOrigin, rcClip, nColorIndex, nBgColorIndex, crText, crBkgnd, pszChars, nOffset + nSelEnd, nCount - nSelEnd, nActualOffset);
			}
		}
		else
		{
			DrawLineHelperImpl(pdc, ptOrigin, rcClip, nColorIndex, nBgColorIndex, crText, crBkgnd, pszChars, nOffset, nCount, nActualOffset);
		}
	}
}

void CCrystalTextView::
GetLineColors(int nLineIndex, COLORREF & crBkgnd,
	COLORREF & crText, bool & bDrawWhitespace)
{
	DWORD dwLineFlags = GetLineFlags(nLineIndex);
	bDrawWhitespace = true;
	crText = RGB(255, 255, 255);
	if (dwLineFlags & LF_EXECUTION)
	{
		crBkgnd = RGB(0, 128, 0);
		return;
	}
	if (dwLineFlags & LF_BREAKPOINT)
	{
		crBkgnd = RGB(255, 0, 0);
		return;
	}
	if (dwLineFlags & LF_INVALID_BREAKPOINT)
	{
		crBkgnd = RGB(128, 128, 0);
		return;
	}
	crBkgnd = CLR_NONE;
	crText = CLR_NONE;
	bDrawWhitespace = false;
}


void CCrystalTextView::DrawScreenLine(CDC *pdc, CPoint &ptOrigin, const CRect &rcClip,
	TEXTBLOCK *pBuf, int nBlocks, int &nActualItem,
	COLORREF crText, COLORREF crBkgnd, bool bDrawWhitespace,
	LPCTSTR pszChars, int nOffset, int nCount, int &nActualOffset, CPoint ptTextPos)
{
	CPoint	originalOrigin = ptOrigin;
	CPoint	ptOriginZeroWidthBlock;
	CRect		frect = rcClip;
	const int nLineLength = GetViewableLineLength(ptTextPos.y);
	const int nLineHeight = GetLineHeight();
	int nBgColorIndexZeorWidthBlock;
	bool bPrevZeroWidthBlock = false;
	static const int ZEROWIDTHBLOCK_WIDTH = 2;

	frect.top = ptOrigin.y;
	frect.bottom = frect.top + nLineHeight;

	ASSERT(nActualItem < nBlocks);

	if (nBlocks > 0 && nActualItem < nBlocks - 1 &&
		pBuf[nActualItem + 1].m_nCharPos >= nOffset &&
		pBuf[nActualItem + 1].m_nCharPos <= nOffset + nCount)
	{
		ASSERT(pBuf[nActualItem].m_nCharPos >= 0 &&
			pBuf[nActualItem].m_nCharPos <= nLineLength);

		int I = 0;
		for (I = nActualItem; I < nBlocks - 1 &&
			pBuf[I + 1].m_nCharPos <= nOffset + nCount; I++)
		{
			ASSERT(pBuf[I].m_nCharPos >= 0 && pBuf[I].m_nCharPos <= nLineLength);

			int nOffsetToUse = (nOffset > pBuf[I].m_nCharPos) ?
				nOffset : pBuf[I].m_nCharPos;
			if (pBuf[I + 1].m_nCharPos - nOffsetToUse > 0)
			{
				int nOldActualOffset = nActualOffset;
				DrawLineHelper(pdc, ptOrigin, rcClip, pBuf[I].m_nColorIndex, pBuf[I].m_nBgColorIndex, crText, crBkgnd, pszChars,
					(nOffset > pBuf[I].m_nCharPos) ? nOffset : pBuf[I].m_nCharPos,
					pBuf[I + 1].m_nCharPos - nOffsetToUse,
					nActualOffset, CPoint(nOffsetToUse, ptTextPos.y));
				if (bPrevZeroWidthBlock)
				{
					CRect rcClipZeroWidthBlock(ptOriginZeroWidthBlock.x, rcClip.top, ptOriginZeroWidthBlock.x + ZEROWIDTHBLOCK_WIDTH, rcClip.bottom);
					DrawLineHelper(pdc, ptOriginZeroWidthBlock, rcClipZeroWidthBlock, pBuf[I].m_nColorIndex, nBgColorIndexZeorWidthBlock, crText, crBkgnd, pszChars,
						(nOffset > pBuf[I].m_nCharPos) ? nOffset : pBuf[I].m_nCharPos,
						pBuf[I + 1].m_nCharPos - nOffsetToUse,
						nOldActualOffset, CPoint(nOffsetToUse, ptTextPos.y));
					bPrevZeroWidthBlock = false;
				}
			}
			else
			{
				if (!bPrevZeroWidthBlock)
				{
					int nBgColorIndex = pBuf[I].m_nBgColorIndex;
					COLORREF clrBkColor;
					if (crBkgnd == CLR_NONE || nBgColorIndex & COLORINDEX_APPLYFORCE)
						clrBkColor = GetColor(nBgColorIndex);
					else
						clrBkColor = crBkgnd;
					pdc->FillSolidRect(ptOrigin.x, ptOrigin.y, ZEROWIDTHBLOCK_WIDTH, GetLineHeight(), clrBkColor);
					ptOriginZeroWidthBlock = ptOrigin;
					nBgColorIndexZeorWidthBlock = pBuf[I].m_nBgColorIndex;
					bPrevZeroWidthBlock = true;
				}
			}
			if (ptOrigin.x > rcClip.right)
				break;
		}

		nActualItem = I;

		ASSERT(pBuf[nActualItem].m_nCharPos >= 0 &&
			pBuf[nActualItem].m_nCharPos <= nLineLength);

		if (nOffset + nCount - pBuf[nActualItem].m_nCharPos > 0)
		{
			int nOldActualOffset = nActualOffset;
			DrawLineHelper(pdc, ptOrigin, rcClip, pBuf[nActualItem].m_nColorIndex, pBuf[nActualItem].m_nBgColorIndex,
				crText, crBkgnd, pszChars, pBuf[nActualItem].m_nCharPos,
				nOffset + nCount - pBuf[nActualItem].m_nCharPos,
				nActualOffset, CPoint(pBuf[nActualItem].m_nCharPos, ptTextPos.y));
			if (bPrevZeroWidthBlock)
			{
				CRect rcClipZeroWidthBlock(ptOriginZeroWidthBlock.x, rcClip.top, ptOriginZeroWidthBlock.x + ZEROWIDTHBLOCK_WIDTH, rcClip.bottom);
				DrawLineHelper(pdc, ptOriginZeroWidthBlock, rcClipZeroWidthBlock, pBuf[nActualItem].m_nColorIndex, nBgColorIndexZeorWidthBlock,
					crText, crBkgnd, pszChars, pBuf[nActualItem].m_nCharPos,
					nOffset + nCount - pBuf[nActualItem].m_nCharPos,
					nOldActualOffset, CPoint(pBuf[nActualItem].m_nCharPos, ptTextPos.y));
				bPrevZeroWidthBlock = false;
			}
		}
		else
		{
			if (!bPrevZeroWidthBlock)
			{
				int nBgColorIndex = pBuf[nActualItem].m_nBgColorIndex;
				COLORREF clrBkColor;
				if (crBkgnd == CLR_NONE || nBgColorIndex & COLORINDEX_APPLYFORCE)
					clrBkColor = GetColor(nBgColorIndex);
				else
					clrBkColor = crBkgnd;
				pdc->FillSolidRect(ptOrigin.x, ptOrigin.y, ZEROWIDTHBLOCK_WIDTH, GetLineHeight(), clrBkColor);
				bPrevZeroWidthBlock = true;
			}
		}
	}
	else
	{
		DrawLineHelper(
			pdc, ptOrigin, rcClip, pBuf[nActualItem].m_nColorIndex, pBuf[nActualItem].m_nBgColorIndex,
			crText, crBkgnd, pszChars, nOffset, nCount, nActualOffset, ptTextPos);
	}

	// Draw space on the right of the text

	frect.left = ptOrigin.x + (bPrevZeroWidthBlock ? ZEROWIDTHBLOCK_WIDTH : 0);

	if ((m_bFocused || m_bShowInactiveSelection)
		&& !m_bColumnSelection
		&& IsInsideSelBlock(CPoint(nLineLength, ptTextPos.y))
		&& (nOffset + nCount) == nLineLength)
	{
		if (frect.left >= rcClip.left)
		{
			const int nCharWidth = GetCharWidth();
			pdc->FillSolidRect(frect.left, frect.top, nCharWidth, frect.Height(),
				GetColor(COLORINDEX_SELBKGND));
			frect.left += nCharWidth;
		}
	}
	if (frect.left < rcClip.left)
		frect.left = rcClip.left;

	if (frect.right > frect.left)
		pdc->FillSolidRect(frect, bDrawWhitespace ?
			crBkgnd : GetColor(COLORINDEX_WHITESPACE));

	// set origin to beginning of next screen line
	ptOrigin.x = originalOrigin.x;
	ptOrigin.y += nLineHeight;
}
//END SW

void CCrystalTextView::
DrawSingleLine(CDC * pdc, const CRect & rc, int nLineIndex)
{
	const int nCharWidth = GetCharWidth();
	ASSERT(nLineIndex >= -1 && nLineIndex < GetLineCount());

	if (nLineIndex == -1)
	{
		//  Draw line beyond the text
		pdc->FillSolidRect(rc, GetColor(COLORINDEX_WHITESPACE));
		return;
	}

	//  Acquire the background color for the current line
	bool bDrawWhitespace = false;
	COLORREF crBkgnd, crText;
	GetLineColors(nLineIndex, crBkgnd, crText, bDrawWhitespace);

	int nLength = GetViewableLineLength(nLineIndex);
	LPCTSTR pszChars = GetLineChars(nLineIndex);

	//  Parse the line
	DWORD dwCookie = GetParseCookie(nLineIndex - 1);
	TEXTBLOCK *pBuf = new TEXTBLOCK[(nLength + 1) * 3]; // be aware of nLength == 0
	int nBlocks = 0;

	// insert at least one textblock of normal color at the beginning
	pBuf[0].m_nCharPos = 0;
	pBuf[0].m_nColorIndex = COLORINDEX_NORMALTEXT;
	pBuf[0].m_nBgColorIndex = COLORINDEX_BKGND;
	nBlocks++;

	(*m_ParseCookies)[nLineIndex] = ParseLine(dwCookie, nLineIndex, pBuf, nBlocks);
	ASSERT((*m_ParseCookies)[nLineIndex] != -1);

	TEXTBLOCK *pAddedBuf;
	int nAddedBlocks = GetAdditionalTextBlocks(nLineIndex, pAddedBuf);

	TEXTBLOCK *pMergedBuf;
	int nMergedBlocks = MergeTextBlocks(pBuf, nBlocks, pAddedBuf, nAddedBlocks, pMergedBuf);

	delete[] pBuf;
	delete[] pAddedBuf;

	pBuf = pMergedBuf;
	nBlocks = nMergedBlocks;

	int nActualItem = 0;
	int nActualOffset = 0;
	// Wrap the line
	IntArray anBreaks(nLength);
	int nBreaks = 0;

	WrapLineCached(nLineIndex, GetScreenChars(), anBreaks.GetData(), nBreaks);

	//  Draw the line text
	CPoint origin(rc.left - m_nOffsetChar * nCharWidth, rc.top);
	if (crBkgnd != CLR_NONE)
		pdc->SetBkColor(crBkgnd);
	if (crText != CLR_NONE)
		pdc->SetTextColor(crText);

	if (nBreaks > 0)
	{
		// Draw all the screen lines of the wrapped line
		ASSERT(anBreaks[0] < nLength);

		// draw start of line to first break
		DrawScreenLine(
			pdc, origin, rc,
			pBuf, nBlocks, nActualItem,
			crText, crBkgnd, bDrawWhitespace,
			pszChars, 0, anBreaks[0], nActualOffset, CPoint(0, nLineIndex));

		// draw from first break to last break
		int i = 0;
		for (i = 0; i < nBreaks - 1; i++)
		{
			ASSERT(anBreaks[i] >= 0 && anBreaks[i] < nLength);
			DrawScreenLine(
				pdc, origin, rc,
				pBuf, nBlocks, nActualItem,
				crText, crBkgnd, bDrawWhitespace,
				pszChars, anBreaks[i], anBreaks[i + 1] - anBreaks[i],
				nActualOffset, CPoint(anBreaks[i], nLineIndex));
		}

		// draw from last break till end of line
		DrawScreenLine(
			pdc, origin, rc,
			pBuf, nBlocks, nActualItem,
			crText, crBkgnd, bDrawWhitespace,
			pszChars, anBreaks[i], nLength - anBreaks[i],
			nActualOffset, CPoint(anBreaks[i], nLineIndex));
	}
	else
		DrawScreenLine(
			pdc, origin, rc,
			pBuf, nBlocks, nActualItem,
			crText, crBkgnd, bDrawWhitespace,
			pszChars, 0, nLength, nActualOffset, CPoint(0, nLineIndex));

	delete[] pBuf;

	// Draw empty sublines
	int nEmptySubLines = GetEmptySubLines(nLineIndex);
	if (nEmptySubLines > 0)
	{
		CRect frect = rc;
		frect.top = frect.bottom - nEmptySubLines * GetLineHeight();
		pdc->FillSolidRect(frect, crBkgnd == CLR_NONE ? GetColor(COLORINDEX_WHITESPACE) : crBkgnd);
	}
}

#pragma warning ( default : 4100 )
