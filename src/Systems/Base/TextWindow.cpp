// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
// -----------------------------------------------------------------------

#include "Precompiled.hpp"

// -----------------------------------------------------------------------

#include "libReallive/defs.h"
#include "Systems/Base/TextWindow.hpp"

#include "MachineBase/RLMachine.hpp"
#include "Systems/Base/GraphicsSystem.hpp"
#include "Systems/Base/SelectionElement.hpp"
#include "Systems/Base/Surface.hpp"
#include "Systems/Base/System.hpp"
#include "Systems/Base/SystemError.hpp"
#include "Systems/Base/TextSystem.hpp"
#include "Systems/Base/TextWaku.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Graphics.hpp"
#include "Utilities/StringUtilities.hpp"
#include "libReallive/gameexe.h"

#include <algorithm>
#include <boost/bind.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "utf8.h"

using boost::bind;
using boost::ref;
using boost::shared_ptr;
using std::cerr;
using std::endl;
using std::ostringstream;
using std::setfill;
using std::setw;
using std::vector;

// -----------------------------------------------------------------------
// TextWindow
// -----------------------------------------------------------------------

TextWindow::TextWindow(System& system, int window_num)
    : window_num_(window_num), ruby_begin_point_(-1), current_line_number_(0),
      current_indentation_in_pixels_(0), last_token_was_name_(false),
      use_indentation_(0), colour_(),
      filter_(0), is_visible_(0), in_selection_mode_(0), next_id_(0),
      system_(system) {
  Gameexe& gexe = system.gameexe();

  // POINT
  Size size = getScreenSize(gexe);
  screen_width_ = size.width();
  screen_height_ = size.height();

  // Base form for everything to follow.
  GameexeInterpretObject window(gexe("WINDOW", window_num));

  // Handle: #WINDOW.index.ATTR_MOD, #WINDOW_ATTR, #WINDOW.index.ATTR
  window_attr_mod_ = window("ATTR_MOD");
  if (window_attr_mod_ == 0)
    setRGBAF(system.text().windowAttr());
  else
    setRGBAF(window("ATTR"));

  setFontSizeInPixels(window("MOJI_SIZE"));
  setWindowSizeInCharacters(window("MOJI_CNT"));
  setSpacingBetweenCharacters(window("MOJI_REP"));
  setRubyTextSize(window("LUBY_SIZE").to_int(0));
  setTextboxPadding(window("MOJI_POS"));

  setWindowPosition(window("POS"));

  setDefaultTextColor(gexe("COLOR_TABLE", 0));

  // INDENT_USE appears to default to on. See the first scene in the
  // game with Nagisa, paying attention to indentation; then check the
  // Gameexe.ini.
  setUseIndentation(window("INDENT_USE").to_int(1));

  setKeycurMod(window("KEYCUR_MOD"));
  setActionOnPause(window("R_COMMAND_MOD"));

  // Main textbox waku
  waku_set_ = window("WAKU_SETNO").to_int(0);
  textbox_waku_.reset(TextWaku::Create(system_, *this, waku_set_, 0));

  // Name textbox if that setting has been enabled.
  setNameMod(window("NAME_MOD").to_int(0));
  if (name_mod_ == 1 && window("NAME_WAKU_SETNO").exists()) {
    name_waku_set_ = window("NAME_WAKU_SETNO");
    namebox_waku_.reset(TextWaku::Create(system_, *this, name_waku_set_, 0));
    setNameSpacingBetweenCharacters(window("NAME_MOJI_REP"));
    setNameboxPadding(window("NAME_MOJI_POS"));
    // Ignoring NAME_WAKU_MIN for now
    setNameboxPosition(window("NAME_POS"));
    name_waku_dir_set_ = window("NAME_WAKU_DIR").to_int(0);
    namebox_centering_ = window("NAME_CENTERING").to_int(0);
    minimum_namebox_size_ = window("NAME_MOJI_MIN").to_int(4);
    name_size_ = window("NAME_MOJI_SIZE");
  }
}

// -----------------------------------------------------------------------

TextWindow::~TextWindow() {}

// -----------------------------------------------------------------------

void TextWindow::execute() {
  if (isVisible() && !system_.graphics().interfaceHidden()) {
    textbox_waku_->execute();
  }
}

// -----------------------------------------------------------------------

void TextWindow::setTextboxPadding(const vector<int>& pos_data) {
  upper_box_padding_ = pos_data.at(0);
  lower_box_padding_ = pos_data.at(1);
  left_box_padding_ = pos_data.at(2);
  right_box_padding_ = pos_data.at(3);
}

// -----------------------------------------------------------------------

void TextWindow::setName(const std::string& utf8name,
                         const std::string& next_char) {
  if (name_mod_ == 0) {
    // Display the name in one pass
    printTextToFunction(bind(&TextWindow::displayChar, ref(*this), _1, _2),
                        utf8name, next_char);
    setIndentation();
  }

  setNameWithoutDisplay(utf8name);
}

// -----------------------------------------------------------------------

void TextWindow::setNameWithoutDisplay(const std::string& utf8name) {
  if (name_mod_ == 1) {
    namebox_characters_ = 0;
    try {
      namebox_characters_ = utf8::distance(utf8name.begin(), utf8name.end());
    } catch(...) {
      // If utf8name isn't a real UTF-8 string, possibly overestimate:
      namebox_characters_ = utf8name.size();
    }

    namebox_characters_ = std::max(namebox_characters_, minimum_namebox_size_);

    renderNameInBox(utf8name);
  }

  last_token_was_name_ = true;
}

// -----------------------------------------------------------------------

void TextWindow::setDefaultTextColor(const vector<int>& colour) {
  default_colour_ = RGBColour(colour.at(0), colour.at(1), colour.at(2));
}

// -----------------------------------------------------------------------

void TextWindow::setFontColor(const vector<int>& colour) {
  font_colour_ = RGBColour(colour.at(0), colour.at(1), colour.at(2));
}

// -----------------------------------------------------------------------

void TextWindow::setWindowSizeInCharacters(const vector<int>& pos_data) {
  x_window_size_in_chars_ = pos_data.at(0);
  y_window_size_in_chars_ = pos_data.at(1);
}

// -----------------------------------------------------------------------

void TextWindow::setSpacingBetweenCharacters(const vector<int>& pos_data) {
  x_spacing_ = pos_data.at(0);
  y_spacing_ = pos_data.at(1);
}

// -----------------------------------------------------------------------

void TextWindow::setWindowPosition(const vector<int>& pos_data) {
  origin_ = pos_data.at(0);
  x_distance_from_origin_ = pos_data.at(1);
  y_distance_from_origin_ = pos_data.at(2);
}

// -----------------------------------------------------------------------

Size TextWindow::textWindowSize() const {
  return Size((x_window_size_in_chars_ *
               (font_size_in_pixels_ + x_spacing_)),
              (y_window_size_in_chars_ *
               (font_size_in_pixels_ + y_spacing_ + ruby_size_)));
}

// -----------------------------------------------------------------------

int TextWindow::boxX1() const {
  switch (origin_) {
  case 0:
  case 2:
    // Error checking. If the size places the text box off screen, then we
    // return 0. Tested this out in ALMA.
    if (x_distance_from_origin_ + boxSize().width() > screen_width_)
      return 0;

    return x_distance_from_origin_;
  case 1:
  case 3:
    return screen_width_ - boxSize().width();
  default:
    throw SystemError("Invalid origin");
  };
}

// -----------------------------------------------------------------------

int TextWindow::boxY1() const {
  switch (origin_) {
  case 0:  // Top and left
  case 1:  // Top and right
    // Error checking. If the size places the text box off screen, then we
    // return 0. Tested this out in ALMA.
    if (y_distance_from_origin_ + boxSize().height() > screen_height_)
      return 0;

    return y_distance_from_origin_;
  case 2:  // Bottom and left
  case 3:  // Bottom and right
    return screen_height_ - boxSize().height();
  default:
    throw SystemError("Invalid origin");
  }
}

// -----------------------------------------------------------------------

Size TextWindow::boxSize() const {
  // This is an estimate; it was what I was using before and worked fine for
  // all the KEY games I orriginally targeted, but broke on ALMA.
  //
  // TODO(erg): This just needs to be adjusted to be correct.
  return textWindowSize() + Size(left_box_padding_ - right_box_padding_,
                                 upper_box_padding_ - lower_box_padding_);
}

// -----------------------------------------------------------------------

int TextWindow::textX1() const {
  return boxX1() + left_box_padding_;
}

// -----------------------------------------------------------------------

int TextWindow::textY1() const {
  return boxY1() + upper_box_padding_;
}

// -----------------------------------------------------------------------

int TextWindow::textX2() const {
  return textX1() + textWindowSize().width() + right_box_padding_;
}

// -----------------------------------------------------------------------

int TextWindow::textY2() const {
  return textY1() + textWindowSize().height() + lower_box_padding_;
}

// -----------------------------------------------------------------------

int TextWindow::nameboxX1() const {
  return boxX1() + namebox_x_offset_;
}

// -----------------------------------------------------------------------

int TextWindow::nameboxY1() const {
  // We cheat with the size calculation here.
  return boxY1() + namebox_y_offset_ -
      (2 * vertical_namebox_padding_ + name_size_);
}

// -----------------------------------------------------------------------

// THIS IS A HACK! THIS IS SUCH AN UGLY HACK. ALL OF THE NAMEBOX POSITIONING
// CODE SIMPLY NEEDS TO BE REDONE.
Size TextWindow::nameboxSize() {
  shared_ptr<Surface> name_surface = nameSurface();
  return Size(2 * horizontal_namebox_padding_ +
              namebox_characters_ * name_size_,
              2 * vertical_namebox_padding_ + name_size_);
}

// -----------------------------------------------------------------------

void TextWindow::setNameSpacingBetweenCharacters(
    const std::vector<int>& pos_data) {
  name_x_spacing_ = pos_data.at(0);
}

// -----------------------------------------------------------------------

void TextWindow::setNameboxPadding(const std::vector<int>& pos_data) {
  horizontal_namebox_padding_ = pos_data.at(0);
  vertical_namebox_padding_ = pos_data.at(1);
}

// -----------------------------------------------------------------------

void TextWindow::setNameboxPosition(const vector<int>& pos_data) {
  namebox_x_offset_ = pos_data.at(0);
  namebox_y_offset_ = pos_data.at(1);
}

// -----------------------------------------------------------------------

void TextWindow::setKeycurMod(const vector<int>& keycur) {
  keycursor_type_ = keycur.at(0);
  keycursor_pos_ = Point(keycur.at(1), keycur.at(2));
}

// -----------------------------------------------------------------------

Point TextWindow::keycursorPosition() const {
  switch (keycursor_type_) {
  case 0:
    return Point(textX2(), textY2());
  case 1:
    return Point(text_insertion_point_x_, text_insertion_point_y_);
  case 2:
    return Point(textX1(), textY1()) + keycursor_pos_;
  default:
    throw SystemError("Invalid keycursor type");
  }
}

// -----------------------------------------------------------------------

/**
 * @todo Make this pass the \#WINDOW_ATTR colour off wile rendering the
 *       waku_backing.
 */
void TextWindow::render(std::ostream* tree) {
  shared_ptr<Surface> text_surface = textSurface();

  if (text_surface && isVisible()) {
    Size surface_size = text_surface->size();

    // POINT
    Point box(boxX1(), boxY1());

    if (tree) {
      *tree << "  Text Window #" << window_num_ << endl;
    }

    textbox_waku_->render(tree, box, surface_size);

    int x = textX1();
    int y = textY1();

    if (inSelectionMode()) {
      for_each(selections_.begin(), selections_.end(),
               bind(&SelectionElement::render, _1));
    } else {
      shared_ptr<Surface> name_surface = nameSurface();
      if (name_surface) {
        Point namebox_location(nameboxX1(), nameboxY1());
        Size namebox_size = nameboxSize();

        if (namebox_waku_) {
          // TODO(erg): The waku needs to be adjusted to be the minimum size of
          // the window in characters
          namebox_waku_->render(tree, namebox_location, namebox_size);
        }

        Point insertion_point =
            namebox_location +
            Point((namebox_size.width() / 2) -
                  (name_surface->size().width() / 2),
                  (namebox_size.height() / 2) -
                  (name_surface->size().height() / 2));
        name_surface->renderToScreen(
            name_surface->rect(),
            Rect(insertion_point, name_surface->size()),
            255);

        if (tree) {
          *tree << "     Name Area: " << Rect(namebox_location, namebox_size)
                << endl;
        }
      }

      text_surface->renderToScreen(
        Rect(Point(0, 0), surface_size),
        Rect(Point(x, y), surface_size),
        255);

      if (tree) {
        *tree << "    Text Area: " << Rect(Point(x, y), surface_size) << endl;
      }
    }
  }
}

// -----------------------------------------------------------------------

void TextWindow::clearWin() {
  text_insertion_point_x_ = 0;
  text_insertion_point_y_ = rubyTextSize();
  current_indentation_in_pixels_ = 0;
  current_line_number_ = 0;
  ruby_begin_point_ = -1;
  font_colour_ = default_colour_;
}

// -----------------------------------------------------------------------

bool TextWindow::isFull() const {
  return current_line_number_ >= y_window_size_in_chars_;
}

// -----------------------------------------------------------------------

void TextWindow::hardBrake() {
  text_insertion_point_x_ = current_indentation_in_pixels_;
  text_insertion_point_y_ += lineHeight();
  current_line_number_++;
}

// -----------------------------------------------------------------------

void TextWindow::setIndentation() {
  current_indentation_in_pixels_ = text_insertion_point_x_;
}

// -----------------------------------------------------------------------

void TextWindow::resetIndentation() {
  current_indentation_in_pixels_ = 0;
}

// -----------------------------------------------------------------------

void TextWindow::markRubyBegin() {
  ruby_begin_point_ = text_insertion_point_x_;
}

// -----------------------------------------------------------------------

void TextWindow::setRGBAF(const vector<int>& attr) {
  colour_ = RGBAColour(attr.at(0), attr.at(1), attr.at(2), attr.at(3));
  setFilter(attr.at(4));
}

// -----------------------------------------------------------------------

void TextWindow::setMousePosition(const Point& pos) {
  using namespace boost;

  if (inSelectionMode()) {
    for_each(selections_.begin(), selections_.end(),
             bind(&SelectionElement::setMousePosition, _1, pos));
  }

  textbox_waku_->setMousePosition(pos);
}

// -----------------------------------------------------------------------

bool TextWindow::handleMouseClick(RLMachine& machine, const Point& pos,
                                  bool pressed) {
  using namespace boost;

  if (inSelectionMode()) {
    bool found =
      find_if(selections_.begin(), selections_.end(),
              bind(&SelectionElement::handleMouseClick, _1, pos, pressed))
      != selections_.end();

    if(found)
      return true;
  }


  if (isVisible() && !machine.system().graphics().interfaceHidden()) {
    return textbox_waku_->handleMouseClick(machine, pos, pressed);
  }

  return false;
}

// -----------------------------------------------------------------------

void TextWindow::startSelectionMode() {
  in_selection_mode_ = true;
  next_id_ = 0;
}

// -----------------------------------------------------------------------

void TextWindow::setSelectionCallback(const boost::function<void(int)>& in) {
  selection_callback_ = in;
}

// -----------------------------------------------------------------------

void TextWindow::endSelectionMode() {
  in_selection_mode_ = false;
  selection_callback_.clear();
  selections_.clear();
  clearWin();
}

// -----------------------------------------------------------------------

const boost::function<void(int)>& TextWindow::selectionCallback() {
  return selection_callback_;
}
