/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; c-file-offsets: ((substatement-open . 0))  -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
#ifndef nsVoidArray_h___
#define nsVoidArray_h___

//#define DEBUG_VOIDARRAY 1

#include "nscore.h"
#include "nsDebug.h"

// Enumerator callback function. Return PR_FALSE to stop
typedef PRBool (* PR_CALLBACK nsVoidArrayEnumFunc)(void* aElement, void *aData);

/// A basic zero-based array of void*'s that manages its own memory
class nsVoidArray {
public:
  nsVoidArray() NS_HIDDEN;
  nsVoidArray(PRInt32 aCount) NS_HIDDEN;  // initial count of aCount elements set to nsnull
  ~nsVoidArray() NS_HIDDEN;

  NS_HIDDEN_(nsVoidArray&) operator=(const nsVoidArray& other);

  inline NS_HIDDEN_(PRInt32) Count() const {
    return mImpl ? mImpl->mCount : 0;
  }
  // returns the max number that can be held without allocating
  inline NS_HIDDEN_(PRInt32) GetArraySize() const {
    return mImpl ? (PRInt32(mImpl->mBits) & kArraySizeMask) : 0;
  }

  NS_HIDDEN_(void*) FastElementAt(PRInt32 aIndex) const
  {
    NS_ASSERTION(0 <= aIndex && aIndex < Count(), "index out of range");
    return mImpl->mArray[aIndex];
  }

  // This both asserts and bounds-checks, because (1) we don't want
  // people to write bad code, but (2) we don't want to change it to
  // crashing for backwards compatibility.  See bug 96108.
  NS_HIDDEN_(void*) ElementAt(PRInt32 aIndex) const
  {
    NS_ASSERTION(0 <= aIndex && aIndex < Count(), "index out of range");
    return SafeElementAt(aIndex);
  }

  // bounds-checked version
  NS_HIDDEN_(void*) SafeElementAt(PRInt32 aIndex) const
  {
    if (PRUint32(aIndex) >= PRUint32(Count())) // handles aIndex < 0 too
    {
      return nsnull;
    }
    // The bounds check ensures mImpl is non-null.
    return mImpl->mArray[aIndex];
  }

  NS_HIDDEN_(void*) operator[](PRInt32 aIndex) const { return ElementAt(aIndex); }

  NS_HIDDEN_(PRInt32) IndexOf(void* aPossibleElement) const;

  NS_HIDDEN_(PRBool) InsertElementAt(void* aElement, PRInt32 aIndex);
  NS_HIDDEN_(PRBool) InsertElementsAt(const nsVoidArray &other, PRInt32 aIndex);

  NS_HIDDEN_(PRBool) ReplaceElementAt(void* aElement, PRInt32 aIndex);

  // useful for doing LRU arrays, sorting, etc
  NS_HIDDEN_(PRBool) MoveElement(PRInt32 aFrom, PRInt32 aTo);

  NS_HIDDEN_(PRBool) AppendElement(void* aElement) {
    return InsertElementAt(aElement, Count());
  }

  NS_HIDDEN_(PRBool) AppendElements(nsVoidArray& aElements) {
    return InsertElementsAt(aElements, Count());
  }

  NS_HIDDEN_(PRBool) RemoveElement(void* aElement);
  NS_HIDDEN_(PRBool) RemoveElementsAt(PRInt32 aIndex, PRInt32 aCount);
  NS_HIDDEN_(PRBool) RemoveElementAt(PRInt32 aIndex) { return RemoveElementsAt(aIndex,1); }

  NS_HIDDEN_(void)   Clear();

  NS_HIDDEN_(PRBool) SizeTo(PRInt32 aMin);
  // Subtly different - Compact() tries to be smart about whether we
  // should reallocate the array; SizeTo() always reallocates.
  NS_HIDDEN_(void) Compact();

  NS_HIDDEN_(PRBool) EnumerateForwards(nsVoidArrayEnumFunc aFunc, void* aData);
  NS_HIDDEN_(PRBool) EnumerateBackwards(nsVoidArrayEnumFunc aFunc, void* aData);

protected:
  NS_HIDDEN_(PRBool) GrowArrayBy(PRInt32 aGrowBy);

  struct Impl {
    /**
     * Packed bits. The low 30 bits are the array's size.
     * The two highest bits indicate whether or not we "own" mImpl and
     * must free() it when destroyed, and whether we have a preallocated
     * nsAutoVoidArray buffer.
     */
    PRUint32 mBits;

    /**
     * The number of elements in the array
     */
    PRInt32 mCount;

    /**
     * Array data, padded out to the actual size of the array.
     */
    void*   mArray[1];
  };

  Impl* mImpl;
#if DEBUG_VOIDARRAY
  PRInt32 mMaxCount;
  PRInt32 mMaxSize;
  PRBool  mIsAuto;
#endif

  enum {
    kArrayOwnerMask = 1 << 31,
    kArrayHasAutoBufferMask = 1 << 30,
    kArraySizeMask = ~(kArrayOwnerMask | kArrayHasAutoBufferMask)
  };
  enum { kAutoBufSize = 8 };


  // bit twiddlers
  NS_HIDDEN_(void) SetArray(Impl *newImpl, PRInt32 aSize, PRInt32 aCount, PRBool aOwner,
                PRBool aHasAuto);
  inline NS_HIDDEN_(PRBool) IsArrayOwner() const {
    return mImpl && (mImpl->mBits & kArrayOwnerMask);
  }
  inline NS_HIDDEN_(PRBool) HasAutoBuffer() const {
    return mImpl && (mImpl->mBits & kArrayHasAutoBufferMask);
  }

private:
  /// Copy constructors are not allowed
  nsVoidArray(const nsVoidArray& other) NS_HIDDEN;
};

// A zero-based array with a bit of automatic internal storage
class nsAutoVoidArray : public nsVoidArray {
public:
  nsAutoVoidArray() NS_HIDDEN;

  NS_HIDDEN_(void) ResetToAutoBuffer()
  {
    SetArray(reinterpret_cast<Impl*>(mAutoBuf), kAutoBufSize, 0, PR_FALSE,
             PR_TRUE);
  }
  
protected:
  // The internal storage
  char mAutoBuf[sizeof(Impl) + (kAutoBufSize - 1) * sizeof(void*)];
};

#endif /* nsVoidArray_h___ */
