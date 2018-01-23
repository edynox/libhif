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

#include <stdio.h>
#include <sys/stat.h>

#include "../hy-nevra.hpp"
#include "../hy-subject.h"

#include "../utils/sqlite3/sqlite3.hpp"

#include "item_rpm.hpp"
#include "swdb.hpp"
#include "transactionitem.hpp"

static int
file_exists(const char *path)
{
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

static const char *sql_create_tables =
#include "sql/create_tables.sql"
    ;

void
SwdbCreateDatabase(std::shared_ptr< SQLite3 > conn)
{
    conn->exec(sql_create_tables);
}

Swdb::Swdb(std::shared_ptr< SQLite3 > conn)
  : conn{conn}
{
}

void
Swdb::createDatabase()
{
    conn->exec(sql_create_tables);
}

void
Swdb::resetDatabase()
{
    conn->close();
    if (file_exists(getPath().c_str())) {
        remove(getPath().c_str());
    }
    conn->open();
    createDatabase();
}

void
Swdb::initTransaction()
{
    if (transactionInProgress) {
        throw std::logic_error("In progress");
    }
    transactionInProgress = std::unique_ptr< Transaction >(new Transaction(conn));
}

int64_t
Swdb::beginTransaction(int64_t dtBegin,
                       std::string rpmdbVersionBegin,
                       std::string cmdline,
                       int32_t userId)
{
    if (!transactionInProgress) {
        throw std::logic_error("Not in progress");
    }
    transactionInProgress->setDtBegin(dtBegin);
    transactionInProgress->setRpmdbVersionBegin(rpmdbVersionBegin);
    transactionInProgress->setCmdline(cmdline);
    transactionInProgress->setUserId(userId);
    transactionInProgress->save();
    transactionInProgress->saveItems();
    return transactionInProgress->getId();
}

int64_t
Swdb::endTransaction(int64_t dtEnd, std::string rpmdbVersionEnd, bool done)
{
    if (!transactionInProgress) {
        throw std::logic_error("Not in progress");
    }
    transactionInProgress->setDtEnd(dtEnd);
    transactionInProgress->setRpmdbVersionEnd(rpmdbVersionEnd);
    transactionInProgress->setDone(done);
    transactionInProgress->save();
    transactionInProgress->saveItems();
    int64_t result = transactionInProgress->getId();
    transactionInProgress = std::unique_ptr< Transaction >(nullptr);
    return result;
}

std::shared_ptr< TransactionItem >
Swdb::addItem(std::shared_ptr< Item > item,
              const std::string &repoid,
              TransactionItemAction action,
              TransactionItemReason reason)
//            std::shared_ptr<TransactionItem> replacedBy)
{
    if (!transactionInProgress) {
        throw std::logic_error("Not in progress");
    }
    // auto replacedBy = std::make_shared<TransactionItem>(nullptr);
    return transactionInProgress->addItem(item, repoid, action, reason);
}

void
Swdb::setItemDone(std::shared_ptr< TransactionItem > item)
{
    item->setDone(true);

    const char *sql = R"**(
        UPDATE
          trans_item
        SET
          done=1
        WHERE
          id = ?
    )**";

    SQLite3::Statement query(*conn, sql);
    query.bindv(item->getId());
    query.step();
}

int
Swdb::resolveRPMTransactionItemReason(const std::string &name,
                                      const std::string &arch,
                                      int64_t maxTransactionId)
{
    // TODO:
    // -1: latest
    // -2: latest and lastTransaction data in memory
    if (transactionInProgress != nullptr) {
        for (auto i : transactionInProgress->getItems()) {
            auto rpm = std::dynamic_pointer_cast< RPMItem >(i->getItem());
            if (!rpm) {
                continue;
            }
            if (rpm->getName() == name && rpm->getArch() == arch) {
                return static_cast< int >(i->getReason());
            }
        }
    }

    auto result = RPMItem::resolveTransactionItemReason(conn, name, arch, maxTransactionId);
    return static_cast< int >(result);
}

const std::string
Swdb::getRPMRepo(const std::string &nevra)
{
    auto nevraObject = new Nevra;
    if (hy_nevra_possibility(nevra.c_str(), HY_FORM_NEVRA, nevraObject)) {
        return "";
    }
    // TODO: hy_nevra_possibility should set epoch to 0 if epoch is not specified
    // and HY_FORM_NEVRA is used
    if (nevraObject->getEpoch() < 0) {
        nevraObject->setEpoch(0);
    }

    const char *sql = R"**(
        SELECT
            repo.repoid as repoid
        FROM
            trans_item
        JOIN
            rpm USING (item_id)
        JOIN
            repo ON trans_item.repo_id == repo.id
        WHERE
            rpm.name = ?
            AND rpm.epoch = ?
            AND rpm.version = ?
            AND rpm.release = ?
            AND rpm.arch = ?
        ORDER BY
            trans_item.id DESC
        LIMIT 1;
    )**";
    // TODO: where trans.done != 0
    SQLite3::Query query(*conn, sql);
    query.bindv(nevraObject->getName(),
                nevraObject->getEpoch(),
                nevraObject->getVersion(),
                nevraObject->getRelease(),
                nevraObject->getArch());
    if (query.step() == SQLite3::Statement::StepResult::ROW) {
        auto repoid = query.get< std::string >("repoid");
        return repoid;
    }
    return "";
}

std::shared_ptr< const TransactionItem >
Swdb::getRPMTransactionItem(const std::string &nevra)
{
    return RPMItem::getTransactionItem(conn, nevra);
}

std::shared_ptr< const Transaction >
Swdb::getLastTransaction()
{
    const char *sql = R"**(
        SELECT
            id
        FROM
            trans
        ORDER BY
            id DESC
        LIMIT 1
    )**";
    SQLite3::Statement query(*conn, sql);
    if (query.step() == SQLite3::Statement::StepResult::ROW) {
        auto transId = query.get< int64_t >(0);
        auto transaction = std::make_shared< const Transaction >(conn, transId);
        return transaction;
    }
    return nullptr;
}

std::vector< std::shared_ptr< Transaction > >
Swdb::listTransactions()
{
    const char *sql = R"**(
        SELECT
            id
        FROM
            trans
        ORDER BY
            id
    )**";
    SQLite3::Statement query(*conn, sql);
    std::vector< std::shared_ptr< Transaction > > result;
    while (query.step() == SQLite3::Statement::StepResult::ROW) {
        auto transId = query.get< int64_t >(0);
        auto transaction = std::make_shared< Transaction >(conn, transId);
        result.push_back(transaction);
    }
    return result;
}

void
Swdb::addConsoleOutputLine(int fileDescriptor, std::string line)
{
    if (!transactionInProgress) {
        throw std::logic_error("Not in progress");
    }
    transactionInProgress->addConsoleOutputLine(fileDescriptor, line);
}

std::shared_ptr< TransactionItem >
Swdb::getCompsGroupItem(const std::string &groupid)
{
    return CompsGroupItem::getTransactionItem(conn, groupid);
}

std::vector< std::shared_ptr< TransactionItem > >
Swdb::getCompsGroupItemsByPattern(const std::string &pattern)
{
    return CompsGroupItem::getTransactionItemsByPattern(conn, pattern);
}

std::vector< std::string >
Swdb::getPackageCompsGroups(const std::string &packageName)
{
    const char *sql_all_groups = R"**(
        SELECT DISTINCT
            g.groupid
        FROM
            comps_group g
        JOIN
            comps_group_package p ON p.group_id = g.item_id
        WHERE
            p.name = ?
            AND p.installed = 1
        ORDER BY
            g.groupid
    )**";

    const char *sql_trans_items = R"**(
        SELECT
            ti.action as action,
            ti.reason as reason,
            i.item_id as group_id
        FROM
            trans_item ti
        JOIN
            comps_group i USING (item_id)
        JOIN
            trans t ON ti.trans_id = t.id
        WHERE
            t.done = 1
            AND ti.action not in (3, 5, 7)
            AND i.groupid = ?
        ORDER BY
            ti.trans_id DESC
        LIMIT 1
    )**";

    const char *sql_group_package = R"**(
        SELECT
            p.name
        FROM
            comps_group_package p
        WHERE
            p.group_id = ?
            AND p.installed = 1
    )**";

    std::vector< std::string > result;

    // list all relevant groups
    SQLite3::Query query_all_groups(*conn, sql_all_groups);
    query_all_groups.bindv(packageName);

    while (query_all_groups.step() == SQLite3::Statement::StepResult::ROW) {
        auto groupid = query_all_groups.get< std::string >("groupid");
        SQLite3::Query query_trans_items(*conn, sql_trans_items);
        query_trans_items.bindv(groupid);
        if (query_trans_items.step() == SQLite3::Statement::StepResult::ROW) {
            auto action =
                static_cast< TransactionItemAction >(query_trans_items.get< int64_t >("action"));
            // if the last record is group removal, skip
            if (action == TransactionItemAction::REMOVE) {
                continue;
            }
            auto groupId = query_trans_items.get< int64_t >("group_id");
            SQLite3::Query query_group_package(*conn, sql_group_package);
            query_group_package.bindv(groupId);
            if (query_group_package.step() == SQLite3::Statement::StepResult::ROW) {
                result.push_back(groupid);
            }
        }
    }
    return result;
}

std::vector< std::string >
Swdb::getCompsGroupEnvironments(const std::string &groupId)
{
    const char *sql_all_environments = R"**(
        SELECT DISTINCT
            e.environmentid
        FROM
            comps_environment e
        JOIN
            comps_environment_group g ON g.environment_id = e.item_id
        WHERE
            g.groupid = ?
            AND g.installed = 1
        ORDER BY
            e.environmentid
    )**";

    const char *sql_trans_items = R"**(
        SELECT
            ti.action as action,
            ti.reason as reason,
            i.item_id as environment_id
        FROM
            trans_item ti
        JOIN
            comps_environment i USING (item_id)
        JOIN
            trans t ON ti.trans_id = t.id
        WHERE
            t.done = 1
            AND ti.action not in (3, 5, 7)
            AND i.environmentid = ?
        ORDER BY
            ti.trans_id DESC
        LIMIT 1
    )**";

    const char *sql_environment_group = R"**(
        SELECT
            g.groupid
        FROM
            comps_environment_group g
        WHERE
            g.environment_id = ?
            AND g.installed = 1
    )**";

    std::vector< std::string > result;

    // list all relevant groups
    SQLite3::Query query_all_environments(*conn, sql_all_environments);
    query_all_environments.bindv(groupId);

    while (query_all_environments.step() == SQLite3::Statement::StepResult::ROW) {
        auto envid = query_all_environments.get< std::string >("environmentid");
        SQLite3::Query query_trans_items(*conn, sql_trans_items);
        query_trans_items.bindv(envid);
        if (query_trans_items.step() == SQLite3::Statement::StepResult::ROW) {
            auto action =
                static_cast< TransactionItemAction >(query_trans_items.get< int64_t >("action"));
            // if the last record is group removal, skip
            if (action == TransactionItemAction::REMOVE) {
                continue;
            }
            auto envId = query_trans_items.get< int64_t >("environment_id");
            SQLite3::Query query_environment_group(*conn, sql_environment_group);
            query_environment_group.bindv(envId);
            if (query_environment_group.step() == SQLite3::Statement::StepResult::ROW) {
                result.push_back(envid);
            }
        }
    }
    return result;
}

std::shared_ptr< TransactionItem >
Swdb::getCompsEnvironmentItem(const std::string &envid)
{
    return CompsEnvironmentItem::getTransactionItem(conn, envid);
}

std::vector< std::shared_ptr< TransactionItem > >
Swdb::getCompsEnvironmentItemsByPattern(const std::string &pattern)
{
    return CompsEnvironmentItem::getTransactionItemsByPattern(conn, pattern);
}
