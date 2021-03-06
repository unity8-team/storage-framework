#!/usr/bin/python3
# Copyright (C) 2016 Canonical Ltd
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Authors: James Henstridge <james.henstridge@canonical.com>

"""A fake version of the OnlineAccounts D-Bus service."""

import sys

import dbus.service
import dbus.mainloop.glib
from gi.repository import GLib

BUS_NAME = "com.ubuntu.OnlineAccounts.Manager"
OBJECT_PATH = "/com/ubuntu/OnlineAccounts/Manager"
OA_IFACE = "com.ubuntu.OnlineAccounts.Manager"

TEST_IFACE = "com.canonical.StorageFramework.Testing"

AUTH_OAUTH1 = 1
AUTH_OAUTH2 = 2
AUTH_PASSWORD = 3
AUTH_SASL = 4

CHANGE_TYPE_ENABLED = 0
CHANGE_TYPE_DISABLED = 1
CHANGE_TYPE_CHANGED = 2

class OAuth1:
    method = AUTH_OAUTH1
    def __init__(self, consumer_key, consumer_secret, token, token_secret, signature_method="HMAC-SHA1"):
        self.consumer_key = consumer_key
        self.consumer_secret = consumer_secret
        self.token = token
        self.token_secret = token_secret
        self.signature_method = signature_method

    def serialise(self, interactive, invalidate):
        return dbus.Dictionary({
            "ConsumerKey": dbus.String(self.consumer_key),
            "ConsumerSecret": dbus.String(self.consumer_secret),
            "Token": dbus.String(self.token),
            "TokenSecret": dbus.String(self.token_secret),
            "SignatureMethod": dbus.String(self.signature_method),
        }, signature="sv")

class OAuth2:
    method = AUTH_OAUTH2
    def __init__(self, access_token, expires_in=0, granted_scopes=[]):
        self.access_token = access_token
        self.expires_in = expires_in
        self.granted_scopes = granted_scopes

    def serialise(self, interactive, invalidate):
        return dbus.Dictionary({
            "AccessToken": dbus.String(self.access_token),
            "ExpiresIn": dbus.Int32(self.expires_in),
            "GrantedScopes": dbus.Array(self.granted_scopes, signature="s"),
        }, signature="sv")

class Password:
    method = AUTH_PASSWORD
    def __init__(self, username, password):
        self.username = username
        self.password = password

    def serialise(self, interactive, invalidate):
        return dbus.Dictionary({
            "Username": dbus.String(self.username),
            "Password": dbus.String(self.password),
        }, signature="sv")

class CredentialsError:
    def __init__(self, method, error):
        assert error in {"NoAccount", "UserCanceled",
                         "PermissionDenied", "InteractionRequired"}
        self.method = method
        self.error = "com.ubuntu.OnlineAccounts.Error." + error

    def serialise(self, interactive, invalidate):
        raise dbus.DBusException("Error", name=self.error)

class CredentialsByMode:
    def __init__(self, noninteractive, interactive, refresh):
        self.method = noninteractive.method
        self.noninteractive = noninteractive
        self.interactive = interactive
        self.refresh = refresh

    def serialise(self, interactive, invalidate):
        if invalidate:
            return self.refresh.serialise(interactive, invalidate)
        elif interactive:
            return self.interactive.serialise(interactive, invalidate)
        else:
            return self.noninteractive.serialise(interactive, invalidate)

class Account:
    def __init__(self, account_id, name, service_id, credentials, settings=None):
        self.account_id = account_id
        self.name = name
        self.service_id = service_id
        self.credentials = credentials
        self.settings = settings

    def serialise(self):
        account_info = dbus.Dictionary({
            "displayName": dbus.String(self.name),
            "serviceId": dbus.String(self.service_id),
            "authMethod": dbus.Int32(self.credentials.method),
        }, signature="sv")
        if self.settings is not None:
            for key, value in self.settings.items():
                account_info['settings/' + key] = value
        return (dbus.UInt32(self.account_id), account_info)

class Manager(dbus.service.Object):
    def __init__(self, connection, object_path, accounts):
        super(Manager, self).__init__(connection, object_path)
        self.accounts = accounts

    @dbus.service.method(dbus_interface=OA_IFACE,
                         in_signature="a{sv}", out_signature="a(ua{sv})aa{sv}")
    def GetAccounts(self, filters):
        #print("GetAccounts %r" % filters)
        sys.stdout.flush()
        return dbus.Array([a.serialise() for a in self.accounts.values()],
                          signature="a(ua{sv})"), dbus.Array(signature="a{sv}")

    @dbus.service.method(dbus_interface=OA_IFACE,
                         in_signature="usbba{sv}", out_signature="a{sv}")
    def Authenticate(self, account_id, service_id, interactive, invalidate, parameters):
        #print("Authenticate %r %r %r %r %r" % (account_id, service_id, interactive, invalidate, parameters))
        sys.stdout.flush()
        account = self.accounts[account_id, service_id]
        return account.credentials.serialise(interactive, invalidate)

    @dbus.service.method(dbus_interface=OA_IFACE,
                         in_signature="sa{sv}", out_signature="(ua{sv})a{sv}")
    def RequestAccess(self, service_id, parameters):
        #print("RequestAccess %r %r" % (service_id, parameters))
        sys.stdout.flush()
        for account in self.accounts.values():
            if account.service_id == service_id:
                return (account.serialise(),
                        account.credentials.serialise(True, False))
        else:
            raise KeyError(service_id)

    @dbus.service.signal(dbus_interface=OA_IFACE,
                         signature="s(ua{sv})")
    def AccountChanged(self, service_id, account):
        pass

    @dbus.service.method(dbus_interface=TEST_IFACE,
                         in_signature="s", out_signature="")
    def UpdateAccount(self, account_data):
        account = eval(account_data)
        key = (account.account_id, account.service_id)
        exists = key in self.accounts
        self.accounts[key] = account
        info = account.serialise()
        info[1]["changeType"] = dbus.UInt32(
            CHANGE_TYPE_CHANGED if exists else CHANGE_TYPE_ENABLED)
        self.AccountChanged(account.service_id, info)

    @dbus.service.method(dbus_interface=TEST_IFACE,
                         in_signature="us", out_signature="")
    def RemoveAccount(self, account_id, service_id):
        account = self.accounts.pop((account_id, service_id), None)
        if account is not None:
            info = account.serialise()
            info[1]["changeType"] = dbus.UInt32(CHANGE_TYPE_DISABLED)
            self.AccountChanged(account.service_id, info)

class Server:
    def __init__(self, accounts):
        self.accounts = dict(((a.account_id, a.service_id), a)
                             for a in accounts)
        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        self.main_loop = GLib.MainLoop()
        self.connection = dbus.SessionBus()
        # Quit when the bus disconnectes
        self.connection.add_signal_receiver(
            self.main_loop.quit, signal_name="Disconnected",
            path="/org/freedesktop/DBus/Local",
            dbus_interface="org.freedesktop.DBus.Local")
        self.manager = Manager(self.connection, OBJECT_PATH, self.accounts)
        self.bus_name = dbus.service.BusName(BUS_NAME, self.connection,
                                             allow_replacement=True,
                                             replace_existing=True,
                                             do_not_queue=True)

    def run(self):
        try:
            self.main_loop.run()
        except KeyboardInterrupt:
            pass

if __name__ == "__main__":
    accounts = [
        Account(1, "OAuth1 account", "oauth1-service",
                OAuth1("consumer_key", "consumer_secret", "token", "token_secret")),
        Account(2, "OAuth2 account", "oauth2-service",
                OAuth2("access_token", 0, ["scope1", "scope2"])),
        Account(3, "Password account", "password-service",
                Password("user", "pass")),
        Account(4, "Password host account", "password-host-service",
                Password("joe", "secret"),
                {"host": "http://www.example.com/"}),
        Account(10, "Mode dependent account", "mode-service",
                CredentialsByMode(
                    noninteractive=CredentialsError(AUTH_PASSWORD, "InteractionRequired"),
                    interactive=Password("user", "interactive"),
                    refresh=Password("user", "refresh")),
                {"host": "http://www.example.com/"}),
        Account(11, "User cancel account", "user-cancel-service",
                CredentialsError(AUTH_PASSWORD, "UserCanceled")),
        Account(42, "Fake test account", "storage-provider-test",
                OAuth2("fake-test-access-token", 0, [])),
        Account(99, "Fake mcloud account", "storage-provider-mcloud",
                OAuth2("fake-mcloud-access-token", 0, [])),
    ]
    server = Server(accounts)
    server.run()
