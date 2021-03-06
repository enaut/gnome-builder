/* ide-code-index-entries.c
 *
 * Copyright © 2017 Anoop Chandu <anoopchandu96@gmail.com>
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

#define G_LOG_DOMAIN "ide-code-index-entries"

#include "symbols/ide-code-index-entries.h"

G_DEFINE_INTERFACE (IdeCodeIndexEntries, ide_code_index_entries, G_TYPE_OBJECT)

IdeCodeIndexEntry *
ide_code_index_entries_real_get_next_entry (IdeCodeIndexEntries *self)
{
  return NULL;
}

static void
ide_code_index_entries_default_init (IdeCodeIndexEntriesInterface *iface)
{
  iface->get_next_entry = ide_code_index_entries_real_get_next_entry;
}

/**
 * ide_code_index_entries_get_next_entry:
 * @self: An #IdeCodeIndexEntries instance.
 *
 * This will fetch next entry in index.
 *
 * Returns: (transfer full) : An #IdeCodeIndexEntry.
 *
 * Since: 3.26
 */
IdeCodeIndexEntry *
ide_code_index_entries_get_next_entry (IdeCodeIndexEntries *self)
{
  g_return_val_if_fail (IDE_IS_CODE_INDEX_ENTRIES (self), NULL);

  return IDE_CODE_INDEX_ENTRIES_GET_IFACE (self)->get_next_entry (self);
}
