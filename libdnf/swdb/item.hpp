/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef LIBDNF_SWDB_ITEM_HPP
#define LIBDNF_SWDB_ITEM_HPP

#include <string>

#include "libdnf/utils/sqlite3/sqlite3.hpp"

class Item {
public:
    Item() = default;
    Item(SQLite3 & conn);
    virtual ~Item() = default;

    int64_t getId() const noexcept { return id; }
    void setId(int64_t value) { id = value; }

    virtual const std::string & getItemType() const noexcept { return itemType; }
    virtual std::string toStr();
    virtual void save();
    SQLite3 & conn;

protected:
    void dbInsert();

    int64_t id = 0;
    const std::string itemType;
};

#endif // LIBDNF_SWDB_ITEM_HPP