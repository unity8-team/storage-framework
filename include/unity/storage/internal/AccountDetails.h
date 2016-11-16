/*
 * Copyright (C) 2016 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Michi Henning <michi.henning@canonical.com>
 */

#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wctor-dtor-privacy"
#pragma GCC diagnostic ignored "-Wswitch-default"
#include <QDBusArgument>
#pragma GCC diagnostic pop

namespace unity
{
namespace storage
{
namespace internal
{

struct AccountDetails
{
    QString providerId;  // Used as the bus name
    QString objectPath;
    qlonglong id;
    QString serviceId;
    QString displayName;
    QString providerName;
    QString iconName;
};

bool operator==(AccountDetails const& lhs, AccountDetails const& rhs);
bool operator!=(AccountDetails const& lhs, AccountDetails const& rhs);
bool operator<(AccountDetails const& lhs, AccountDetails const& rhs);
bool operator<=(AccountDetails const& lhs, AccountDetails const& rhs);
bool operator>(AccountDetails const& lhs, AccountDetails const& rhs);
bool operator>=(AccountDetails const& lhs, AccountDetails const& rhs);

QDBusArgument& operator<<(QDBusArgument& argument, storage::internal::AccountDetails const& account);
QDBusArgument const& operator>>(QDBusArgument const& argument, storage::internal::AccountDetails& account);

QDBusArgument& operator<<(QDBusArgument& argument, QList<storage::internal::AccountDetails> const& acc_list);
QDBusArgument const& operator>>(QDBusArgument const& argument, QList<storage::internal::AccountDetails>& acc_list);

}  // namespace internal
}  // namespace storage
}  // namespace unity

Q_DECLARE_METATYPE(unity::storage::internal::AccountDetails)