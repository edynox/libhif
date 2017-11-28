/*
 * Copyright (C) 2017 Red Hat, Inc.
 * Author: Eduard Cuba <ecuba@redhat.com>
 *         Martin Hatina <mhatina@redhat.com>
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

#ifndef LIBDNF_SWDB_TYPES_REASON
#define LIBDNF_SWDB_TYPES_REASON

typedef enum {
    REASON_UNKNOWN = 0,
    REASON_DEP = 1,
    REASON_USER = 2,
    REASON_CLEAN = 3, // hawkey compatibility
    REASON_WEAK = 4,
    REASON_GROUP = 5
} Reason;

#endif