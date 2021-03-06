/*
 * Copyright (C) 2017-2018 Red Hat, Inc.
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

#include "transaction.hpp"
#include "item_comps_environment.hpp"
#include "item_comps_group.hpp"
#include "item_rpm.hpp"
#include "transactionitem.hpp"


SwdbPrivate::Transaction::Transaction(SQLite3Ptr conn)
  : libdnf::Transaction(conn)
{
}

void
SwdbPrivate::Transaction::begin()
{
    if (id != 0) {
        throw std::runtime_error("Transaction has already begun!");
    }
    dbInsert();
    saveItems();
}

void
SwdbPrivate::Transaction::finish(bool success)
{
    setDone(success);
    dbUpdate();
}

void
SwdbPrivate::Transaction::dbInsert()
{
    const char *sql =
        "INSERT INTO "
        "  trans ("
        "    dt_begin, "
        "    dt_end, "
        "    rpmdb_version_begin, "
        "    rpmdb_version_end, "
        "    releasever, "
        "    user_id, "
        "    cmdline, "
        "    done, "
        "    id "
        "  ) "
        "VALUES "
        "  (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    SQLite3::Statement query(*conn.get(), sql);
    query.bindv(getDtBegin(),
                getDtEnd(),
                getRpmdbVersionBegin(),
                getRpmdbVersionEnd(),
                getReleasever(),
                getUserId(),
                getCmdline(),
                getDone());
    if (getId() > 0) {
        query.bind(9, getId());
    }
    query.step();
    setId(conn->lastInsertRowID());

    // add used software - has to be added at initialization state
    if (!softwarePerformedWith.empty()) {
        sql = R"**(
            INSERT INTO
                trans_with (
                    trans_id,
                    item_id
                )
            VALUES
                (?, ?)
        )**";
        SQLite3::Statement swQuery(*conn.get(), sql);
        bool first = true;
        for (auto software : softwarePerformedWith) {
            if (!first) {
                swQuery.reset();
            }
            first = false;
            swQuery.bindv(getId(), software->getId());
            swQuery.step();
        }
    }
}

void
SwdbPrivate::Transaction::dbUpdate()
{
    const char *sql =
        "UPDATE "
        "  trans "
        "SET "
        "  dt_begin=?, "
        "  dt_end=?, "
        "  rpmdb_version_begin=?, "
        "  rpmdb_version_end=?, "
        "  releasever=?, "
        "  user_id=?, "
        "  cmdline=?, "
        "  done=? "
        "WHERE "
        "  id = ?";
    SQLite3::Statement query(*conn.get(), sql);
    query.bindv(getDtBegin(),
                getDtEnd(),
                getRpmdbVersionBegin(),
                getRpmdbVersionEnd(),
                getReleasever(),
                getUserId(),
                getCmdline(),
                getDone(),
                getId());
    query.step();
}

TransactionItemPtr
SwdbPrivate::Transaction::addItem(std::shared_ptr< Item > item,
                                  const std::string &repoid,
                                  TransactionItemAction action,
                                  TransactionItemReason reason)
{
    auto trans_item = std::make_shared< TransactionItem >(this);
    trans_item->setItem(item);
    trans_item->setRepoid(repoid);
    trans_item->setAction(action);
    trans_item->setReason(reason);
    items.push_back(trans_item);
    return trans_item;
}

void
SwdbPrivate::Transaction::saveItems()
{
    // TODO: remove all existing items from the database first?
    for (auto i : items) {
        i->save();
    }

    /* this has to be done in a separate loop to make sure
     * that all the items already have ID assigned
     */
    for (auto i : items) {
        i->saveReplacedBy();
    }
}

/**
 * Loader for the transaction items.
 * \return list of transaction items associated with the transaction
 */
std::vector< TransactionItemPtr >
SwdbPrivate::Transaction::getItems()
{
    if (items.empty()) {
        items = libdnf::Transaction::getItems();
    }
    return items;
}

/**
 * Append software to softwarePerformedWith list.
 * Software is saved to the database using save method and therefore
 * all the software has to be added before transaction is saved.
 * \param software RPMItem used to perform the transaction
 */
void
SwdbPrivate::Transaction::addSoftwarePerformedWith(std::shared_ptr< RPMItem > software)
{
    softwarePerformedWith.insert(software);
}

/**
 * Save console output line for current transaction to the database. Transaction has
 *  to be saved in advance, otherwise an exception will be thrown.
 * \param fileDescriptor UNIX file descriptor index (1 = stdout, 2 = stderr).
 * \param line console output content
 */
void
SwdbPrivate::Transaction::addConsoleOutputLine(int fileDescriptor, const std::string &line)
{
    if (!getId()) {
        throw std::runtime_error("Can't add console output to unsaved transaction");
    }

    const char *sql = R"**(
        INSERT INTO
            console_output (
                trans_id,
                file_descriptor,
                line
            )
        VALUES
            (?, ?, ?);
    )**";
    SQLite3::Statement query(*conn, sql);
    query.bindv(getId(), fileDescriptor, line);
    query.step();
}
