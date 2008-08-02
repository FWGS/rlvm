// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libReallive, a dependency of RLVM. 
// 
// -----------------------------------------------------------------------
//
// Copyright (c) 2006, 2007 Peter Jolly
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "defs.h"
#include "scenario.h"
#include "filemap.h"

namespace libReallive {
  
/** 
 * Interface to a loaded SEEN.TXT file.
 * 
 */
class Archive {
  typedef std::map<int, FilePos> scenarios_t;
  typedef std::map<int, Scenario*> accessed_t;
  scenarios_t scenarios;
  accessed_t accessed;
  string name;
  Mapping info;

  /**
   * Now that VisualArts is using per game xor keys, this is equivalent to the
   * game's second level xor key.
   */
  const char* second_level_xor_key_;

  void readTOC();

public:
  /// Read an archive, assuming no per-game xor key. (Used in unit testing).
  Archive(const string& filename);

  /** 
   * Creates an interface to a SEEN.TXT file.
   * 
   * @param filename path to the SEEN.TXT file.
   * @param regname The \#REGNAME key from the game's Gameexe.ini file. Required
   *                to lookup per-game xor key for newer games.
   */
  Archive(const string& filename, const string& regname);
  ~Archive();
  
  typedef std::map<int, FilePos>::const_iterator const_iterator;
  const_iterator begin() { return scenarios.begin(); }
  const_iterator end()   { return scenarios.end(); }
  
  /** 
   * Returns a specific scenario 
   * 
   * @param index The SEEN number to return
   * @return The coresponding Scenario if index exists, or NULL if it doesn't.
   */
  Scenario* scenario(int index);
  
  void reset();
};

}

#endif
