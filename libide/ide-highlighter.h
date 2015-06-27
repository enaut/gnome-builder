/* ide-highlighter.h
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IDE_HIGHLIGHTER_H
#define IDE_HIGHLIGHTER_H

#include <gtk/gtk.h>

#include "ide-buffer.h"
#include "ide-highlight-engine.h"
#include "ide-object.h"
#include "ide-source-view.h"
#include "ide-types.h"

G_BEGIN_DECLS

#define IDE_TYPE_HIGHLIGHTER (ide_highlighter_get_type())

G_DECLARE_INTERFACE (IdeHighlighter, ide_highlighter, IDE, HIGHLIGHTER, IdeObject)

struct _IdeHighlighterInterface
{
  GTypeInterface parent_iface;

  /**
   * IdeHighlighter::update:
   *
   * #IdeHighlighter should call callback() with the range and style-name of
   * the style to apply. Callback will ensure that the style exists and style
   * it appropriately based on the style scheme.
   *
   * If @callback returns %IDE_HIGHLIGHT_STOP, the caller has run out of its
   * time slice and should yield back to the highlight engine.
   *
   * @location should be set to the position that the highlighter got to
   * before yielding back to the engine.
   */
  void (*update) (IdeHighlighter       *self,
                  IdeHighlightCallback  callback,
                  const GtkTextIter    *range_begin,
                  const GtkTextIter    *range_end,
                  GtkTextIter          *location);

  /**
   * IdeHighlighter::set-engine:
   *
   * This vfunc is called when the highlighter is attached to an engine.
   */
  void (*set_engine) (IdeHighlighter     *self,
                      IdeHighlightEngine *engine);
};

void ide_highlighter_update (IdeHighlighter       *self,
                             IdeHighlightCallback  callback,
                             const GtkTextIter    *range_begin,
                             const GtkTextIter    *range_end,
                             GtkTextIter          *location);

G_END_DECLS

#endif /* IDE_HIGHLIGHTER_H */
