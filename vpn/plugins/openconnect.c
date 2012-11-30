/*
 *
 *  ConnMan VPN daemon
 *
 *  Copyright (C) 2007-2012  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>

#include <glib.h>

#define CONNMAN_API_SUBJECT_TO_CHANGE
#include <connman/plugin.h>
#include <connman/log.h>
#include <connman/task.h>
#include <connman/ipconfig.h>
#include <connman/dbus.h>
#include <connman/agent.h>
#include <connman/setting.h>
#include <connman/vpn-dbus.h>

#include "../vpn-provider.h"
#include "../vpn-agent.h"

#include "vpn.h"

struct oc_private_data {
	struct connman_task *task;
	char *if_name;
	vpn_provider_connect_cb_t cb;
	void *user_data;
};

static int oc_notify(DBusMessage *msg, struct vpn_provider *provider)
{
	DBusMessageIter iter, dict;
	const char *reason, *key, *value;
	char *domain = NULL;
	char *addressv4 = NULL, *addressv6 = NULL;
	char *netmask = NULL, *gateway = NULL;
	unsigned char prefix_len = 0;
	struct connman_ipaddress *ipaddress;

	dbus_message_iter_init(msg, &iter);

	dbus_message_iter_get_basic(&iter, &reason);
	dbus_message_iter_next(&iter);

	if (!provider) {
		connman_error("No provider found");
		return VPN_STATE_FAILURE;
	}

	if (strcmp(reason, "connect"))
		return VPN_STATE_DISCONNECT;

	domain = g_strdup(vpn_provider_get_string(provider, "VPN.Domain"));

	dbus_message_iter_recurse(&iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);
		dbus_message_iter_get_basic(&entry, &value);

		if (strcmp(key, "CISCO_CSTP_OPTIONS"))
			DBG("%s = %s", key, value);

		if (!strcmp(key, "VPNGATEWAY"))
			gateway = g_strdup(value);

		if (!strcmp(key, "INTERNAL_IP4_ADDRESS"))
			addressv4 = g_strdup(value);

		if (!strcmp(key, "INTERNAL_IP6_ADDRESS")) {
			addressv6 = g_strdup(value);
			prefix_len = 128;
		}

		if (!strcmp(key, "INTERNAL_IP4_NETMASK"))
			netmask = g_strdup(value);

		if (!strcmp(key, "INTERNAL_IP6_NETMASK")) {
			char *sep;

			/* The netmask contains the address and the prefix */
			sep = strchr(value, '/');
			if (sep != NULL) {
				unsigned char ip_len = sep - value;

				addressv6 = g_strndup(value, ip_len);
				prefix_len = (unsigned char)
						strtol(sep + 1, NULL, 10);
			}
		}

		if (!strcmp(key, "INTERNAL_IP4_DNS") ||
				!strcmp(key, "INTERNAL_IP6_DNS"))
			vpn_provider_set_nameservers(provider, value);

		if (!strcmp(key, "CISCO_PROXY_PAC"))
			vpn_provider_set_pac(provider, value);

		if (domain == NULL && !strcmp(key, "CISCO_DEF_DOMAIN")) {
			g_free(domain);
			domain = g_strdup(value);
		}

		if (g_str_has_prefix(key, "CISCO_SPLIT_INC") == TRUE ||
			g_str_has_prefix(key, "CISCO_IPV6_SPLIT_INC") == TRUE)
			vpn_provider_append_route(provider, key, value);

		dbus_message_iter_next(&dict);
	}

	DBG("%p %p", addressv4, addressv6);

	if (addressv4 != NULL)
		ipaddress = connman_ipaddress_alloc(AF_INET);
	else if (addressv6 != NULL)
		ipaddress = connman_ipaddress_alloc(AF_INET6);
	else
		ipaddress = NULL;

	if (ipaddress == NULL) {
		g_free(addressv4);
		g_free(addressv6);
		g_free(netmask);
		g_free(gateway);
		g_free(domain);

		return VPN_STATE_FAILURE;
	}

	if (addressv4 != NULL)
		connman_ipaddress_set_ipv4(ipaddress, addressv4,
						netmask, gateway);
	else
		connman_ipaddress_set_ipv6(ipaddress, addressv6,
						prefix_len, gateway);
	vpn_provider_set_ipaddress(provider, ipaddress);
	vpn_provider_set_domain(provider, domain);

	g_free(addressv4);
	g_free(addressv6);
	g_free(netmask);
	g_free(gateway);
	g_free(domain);
	connman_ipaddress_free(ipaddress);

	return VPN_STATE_CONNECT;
}

static void request_input_append_cookie(DBusMessageIter *iter,
							void *user_data)
{
	char *str = "string";

	connman_dbus_dict_append_basic(iter, "Type",
				DBUS_TYPE_STRING, &str);
	str = "mandatory";
	connman_dbus_dict_append_basic(iter, "Requirement",
				DBUS_TYPE_STRING, &str);
}

struct request_input_reply {
	struct vpn_provider *provider;
	vpn_provider_auth_cb_t callback;
	void *user_data;
};

static void request_input_cookie_reply(DBusMessage *reply, void *user_data)
{
	struct request_input_reply *cookie_reply = user_data;
	const char *error = NULL;
	char *cookie = NULL;
	char *key;
	DBusMessageIter iter, dict;

	DBG("provider %p", cookie_reply->provider);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		error = dbus_message_get_error_name(reply);
		goto done;
	}

	if (vpn_agent_check_reply_has_dict(reply) == FALSE)
		goto done;

	dbus_message_iter_init(reply, &iter);
	dbus_message_iter_recurse(&iter, &dict);
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, value;

		dbus_message_iter_recurse(&dict, &entry);
		if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
			break;

		dbus_message_iter_get_basic(&entry, &key);

		if (g_str_equal(key, "OpenConnect.Cookie")) {
			dbus_message_iter_next(&entry);
			if (dbus_message_iter_get_arg_type(&entry)
							!= DBUS_TYPE_VARIANT)
				break;
			dbus_message_iter_recurse(&entry, &value);
			if (dbus_message_iter_get_arg_type(&value)
							!= DBUS_TYPE_STRING)
				break;
			dbus_message_iter_get_basic(&value, &cookie);
		}

		dbus_message_iter_next(&dict);
	}

done:
	cookie_reply->callback(cookie_reply->provider, cookie, error,
				cookie_reply->user_data);
	g_free(cookie_reply);
}

typedef void (* request_cb_t)(struct vpn_provider *provider,
					const char *vpncookie,
					const char *error, void *user_data);

static int request_cookie_input(struct vpn_provider *provider,
				request_cb_t callback, void *user_data)
{
	DBusMessage *message;
	const char *path, *agent_sender, *agent_path;
	DBusMessageIter iter;
	DBusMessageIter dict;
	struct request_input_reply *cookie_reply;
	int err;

	connman_agent_get_info(&agent_sender, &agent_path);

	if (provider == NULL || agent_path == NULL || callback == NULL)
		return -ESRCH;

	message = dbus_message_new_method_call(agent_sender, agent_path,
					VPN_AGENT_INTERFACE,
					"RequestInput");
	if (message == NULL)
		return -ENOMEM;

	dbus_message_iter_init_append(message, &iter);

	path = vpn_provider_get_path(provider);
	dbus_message_iter_append_basic(&iter,
				DBUS_TYPE_OBJECT_PATH, &path);

	connman_dbus_dict_open(&iter, &dict);

	connman_dbus_dict_append_dict(&dict, "OpenConnect.Cookie",
			request_input_append_cookie, provider);

	vpn_agent_append_host_and_name(&dict, provider);

	connman_dbus_dict_close(&iter, &dict);

	cookie_reply = g_try_new0(struct request_input_reply, 1);
	if (cookie_reply == NULL) {
		dbus_message_unref(message);
		return -ENOMEM;
	}

	cookie_reply->provider = provider;
	cookie_reply->callback = callback;
	cookie_reply->user_data = user_data;

	err = connman_agent_queue_message(provider, message,
			connman_timeout_input_request(),
			request_input_cookie_reply, cookie_reply);
	if (err < 0 && err != -EBUSY) {
		DBG("error %d sending agent request", err);
		dbus_message_unref(message);
		g_free(cookie_reply);
		return err;
	}

	dbus_message_unref(message);

	return -EINPROGRESS;
}

static int run_connect(struct vpn_provider *provider,
			struct connman_task *task, const char *if_name,
			vpn_provider_connect_cb_t cb, void *user_data,
			const char *vpncookie)
{
	const char *vpnhost, *cafile, *certsha1, *mtu;
	int fd, err = 0, len;

	vpnhost = vpn_provider_get_string(provider, "Host");

	if (vpncookie == NULL) {
		DBG("Cookie missing, cannot connect!");
		err = -EINVAL;
		goto done;
	}

	vpn_provider_set_string(provider, "OpenConnect.Cookie", vpncookie);

	certsha1 = vpn_provider_get_string(provider,
						"OpenConnect.ServerCert");
	if (certsha1)
		connman_task_add_argument(task, "--servercert",
							(char *)certsha1);

	cafile = vpn_provider_get_string(provider, "OpenConnect.CACert");
	mtu = vpn_provider_get_string(provider, "VPN.MTU");

	if (cafile)
		connman_task_add_argument(task, "--cafile",
							(char *)cafile);
	if (mtu)
		connman_task_add_argument(task, "--mtu", (char *)mtu);

	connman_task_add_argument(task, "--syslog", NULL);
	connman_task_add_argument(task, "--cookie-on-stdin", NULL);

	connman_task_add_argument(task, "--script",
				  SCRIPTDIR "/openconnect-script");

	connman_task_add_argument(task, "--interface", if_name);

	connman_task_add_argument(task, (char *)vpnhost, NULL);

	err = connman_task_run(task, vpn_died, provider,
			       &fd, NULL, NULL);
	if (err < 0) {
		connman_error("openconnect failed to start");
		err = -EIO;
		goto done;
	}

	len = strlen(vpncookie);
	if (write(fd, vpncookie, len) != (ssize_t)len ||
			write(fd, "\n", 1) != 1) {
		connman_error("openconnect failed to take cookie on stdin");
		err = -EIO;
		goto done;
	}

done:
	if (cb != NULL)
		cb(provider, user_data, err);

	return err;
}

static void free_private_data(struct oc_private_data *data)
{
	g_free(data->if_name);
	g_free(data);
}

static void request_input_cb(struct vpn_provider *provider,
			const char *vpncookie,
			const char *error, void *user_data)
{
	struct oc_private_data *data = user_data;

	if (vpncookie == NULL)
		DBG("Requesting cookie failed, error %s", error);
	else if (error != NULL)
		DBG("error %s", error);

	run_connect(provider, data->task, data->if_name, data->cb,
		data->user_data, vpncookie);

	free_private_data(data);
}

static int oc_connect(struct vpn_provider *provider,
			struct connman_task *task, const char *if_name,
			vpn_provider_connect_cb_t cb, void *user_data)
{
	const char *vpnhost, *vpncookie;
	int err;

	vpnhost = vpn_provider_get_string(provider, "Host");
	if (vpnhost == NULL) {
		connman_error("Host not set; cannot enable VPN");
		return -EINVAL;
	}

	vpncookie = vpn_provider_get_string(provider, "OpenConnect.Cookie");
	if (vpncookie == NULL) {
		struct oc_private_data *data;

		data = g_try_new0(struct oc_private_data, 1);
		if (data == NULL)
			return -ENOMEM;

		data->task = task;
		data->if_name = g_strdup(if_name);
		data->cb = cb;
		data->user_data = user_data;

		err = request_cookie_input(provider, request_input_cb, data);
		if (err != -EINPROGRESS) {
			free_private_data(data);
			goto done;
		}
		return err;
	}

done:
	return run_connect(provider, task, if_name, cb, user_data, vpncookie);
}

static int oc_save(struct vpn_provider *provider, GKeyFile *keyfile)
{
	const char *setting;

	setting = vpn_provider_get_string(provider,
					"OpenConnect.ServerCert");
	if (setting != NULL)
		g_key_file_set_string(keyfile,
				vpn_provider_get_save_group(provider),
				"OpenConnect.ServerCert", setting);

	setting = vpn_provider_get_string(provider,
					"OpenConnect.CACert");
	if (setting != NULL)
		g_key_file_set_string(keyfile,
				vpn_provider_get_save_group(provider),
				"OpenConnect.CACert", setting);

	setting = vpn_provider_get_string(provider,
					"VPN.MTU");
	if (setting != NULL)
		g_key_file_set_string(keyfile,
				vpn_provider_get_save_group(provider),
				"VPN.MTU", setting);

	return 0;
}

static int oc_error_code(int exit_code)
{

	switch (exit_code) {
	case 1:
		return VPN_PROVIDER_ERROR_CONNECT_FAILED;
	case 2:
		return VPN_PROVIDER_ERROR_LOGIN_FAILED;
	default:
		return VPN_PROVIDER_ERROR_UNKNOWN;
	}
}

static struct vpn_driver vpn_driver = {
	.notify         = oc_notify,
	.connect	= oc_connect,
	.error_code	= oc_error_code,
	.save		= oc_save,
};

static int openconnect_init(void)
{
	return vpn_register("openconnect", &vpn_driver, OPENCONNECT);
}

static void openconnect_exit(void)
{
	vpn_unregister("openconnect");
}

CONNMAN_PLUGIN_DEFINE(openconnect, "OpenConnect VPN plugin", VERSION,
	CONNMAN_PLUGIN_PRIORITY_DEFAULT, openconnect_init, openconnect_exit)
