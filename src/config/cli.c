/* Pi-hole: A black hole for Internet advertisements
*  (c) 2023 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  CLI config routines
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "FTL.h"
#include "config/cli.h"
#include "config/config.h"
#include "config/toml_helper.h"
#include "config/toml_writer.h"
#include "config/dnsmasq_config.h"

#include "log.h"
#include "datastructure.h"

// toml_table_t
#include "tomlc99/toml.h"


// Read a TOML value from a table depending on its type
static bool readStringValue(struct conf_item *conf_item, const char *value)
{
	if(conf_item == NULL || value == NULL)
	{
		log_debug(DEBUG_CONFIG, "readStringValue(%p, %p) called with invalid arguments, skipping",
		          conf_item, value);
		return false;
	}
	switch(conf_item->t)
	{
		case CONF_BOOL:
		{
			if(strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0)
				conf_item->v.b = true;
			else if(strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0)
				conf_item->v.b = false;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: [ true, false, yes, no ]", conf_item->k);
				return false;
			}
			break;
		}
		case CONF_INT:
		{
			int val;
			if(sscanf(value, "%i", &val) == 1)
				conf_item->v.i = val;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: integer", conf_item->k);
				return false;
			}
			break;
		}
		case CONF_UINT:
		{
			unsigned int val;
			if(sscanf(value, "%u", &val) == 1)
				conf_item->v.ui = val;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: unsigned integer", conf_item->k);
				return false;
			}
			break;
		}
		case CONF_UINT16:
		{
			uint16_t val;
			if(sscanf(value, "%hu", &val) == 1)
				conf_item->v.ui = val;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: unsigned integer (16 bit)", conf_item->k);
				return false;
			}
			break;
		}
		case CONF_LONG:
		{
			long val;
			if(sscanf(value, "%li", &val) == 1)
				conf_item->v.l = val;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: long integer", conf_item->k);
				return false;
			}
			break;
		}
		case CONF_ULONG:
		{
			unsigned long val;
			if(sscanf(value, "%lu", &val) == 1)
				conf_item->v.ul = val;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: unsigned long integer", conf_item->k);
				return false;
			}
			break;
		}
		case CONF_DOUBLE:
		{
			double val;
			if(sscanf(value, "%lf", &val) == 1)
				conf_item->v.d = val;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: double", conf_item->k);
				return false;
			}
			break;
		}
		case CONF_STRING:
		case CONF_STRING_ALLOCATED:
		{
			if(conf_item->t == CONF_STRING_ALLOCATED)
					free(conf_item->v.s);
			conf_item->v.s = strdup(value);
			conf_item->t = CONF_STRING_ALLOCATED;
			break;
		}
		case CONF_PASSWORD:
		{
			// Get password hash as allocated string
			char *pwhash = hash_password(value);
			// Get pointer to pwhash instead
			log_info("Pointer to conf_item: %p = %s", conf_item, conf_item->k);
			conf_item--;
			log_info("Pointer to conf_item: %p = %s", conf_item, conf_item->k);
			if(conf_item->t == CONF_STRING_ALLOCATED)
					free(conf_item->v.s);
			conf_item->v.s = pwhash;
			conf_item->t = CONF_STRING_ALLOCATED;
			break;
		}
		case CONF_ENUM_PTR_TYPE:
		{
			const int ptr_type = get_ptr_type_val(value);
			if(ptr_type != -1)
				conf_item->v.ptr_type = ptr_type;
			else
			{
				char *allowed = NULL;
				CONFIG_ITEM_ARRAY(conf_item->a, allowed);
				log_err("Config setting %s is invalid, allowed options are: %s", conf_item->k, allowed);
				free(allowed);
				return false;
			}
			break;
		}
		case CONF_ENUM_BUSY_TYPE:
		{
			const int busy_reply = get_busy_reply_val(value);
			if(busy_reply != -1)
				conf_item->v.busy_reply = busy_reply;
			else
			{
				char *allowed = NULL;
				CONFIG_ITEM_ARRAY(conf_item->a, allowed);
				log_err("Config setting %s is invalid, allowed options are: %s", conf_item->k, allowed);
				free(allowed);
				return false;
			}
			break;
		}
		case CONF_ENUM_BLOCKING_MODE:
		{
			const int blocking_mode = get_blocking_mode_val(value);
			if(blocking_mode != -1)
				conf_item->v.blocking_mode = blocking_mode;
			else
			{
				char *allowed = NULL;
				CONFIG_ITEM_ARRAY(conf_item->a, allowed);
				log_err("Config setting %s is invalid, allowed options are: %s", conf_item->k, allowed);
				free(allowed);
				return false;
			}
			break;
		}
		case CONF_ENUM_REFRESH_HOSTNAMES:
		{
			const int refresh_hostnames = get_refresh_hostnames_val(value);
			if(refresh_hostnames != -1)
				conf_item->v.refresh_hostnames = refresh_hostnames;
			else
			{
				char *allowed = NULL;
				CONFIG_ITEM_ARRAY(conf_item->a, allowed);
				log_err("Config setting %s is invalid, allowed options are: %s", conf_item->k, allowed);
				free(allowed);
				return false;
			}
			break;
		}
		case CONF_ENUM_LISTENING_MODE:
		{
			const int listeningMode = get_listeningMode_val(value);
			if(listeningMode != -1)
				conf_item->v.listeningMode = listeningMode;
			else
			{
				char *allowed = NULL;
				CONFIG_ITEM_ARRAY(conf_item->a, allowed);
				log_err("Config setting %s is invalid, allowed options are: %s", conf_item->k, allowed);
				free(allowed);
				return false;
			}
			break;
		}
		case CONF_ENUM_PRIVACY_LEVEL:
		{
			int val;
			if(sscanf(value, "%i", &val) == 1 && val >= PRIVACY_SHOW_ALL && val <= PRIVACY_MAXIMUM)
				conf_item->v.i = val;
			else
			{
				log_err("Config setting %s is invalid, allowed options are: integer between %d and %d", conf_item->k, PRIVACY_SHOW_ALL, PRIVACY_MAXIMUM);
				return false;
			}
			break;
		}
		case CONF_ENUM_WEB_THEME:
		{
			const int web_theme = get_web_theme_val(value);
			if(web_theme != -1)
				conf_item->v.web_theme = web_theme;
			else
			{
				char *allowed = NULL;
				CONFIG_ITEM_ARRAY(conf_item->a, allowed);
				log_err("Config setting %s is invalid, allowed options are: %s", conf_item->k, allowed);
				free(allowed);
				return false;
			}
			break;
		}
		case CONF_STRUCT_IN_ADDR:
		{
			struct in_addr addr4 = { 0 };
			if(inet_pton(AF_INET, value, &addr4))
				memcpy(&conf_item->v.in_addr, &addr4, sizeof(addr4));
			else
			{
				log_err("Config setting %s is invalid (%s), allowed options are: IPv4 address", conf_item->k, strerror(errno));
				return false;
			}
			break;
		}
		case CONF_STRUCT_IN6_ADDR:
		{
			struct in6_addr addr6 = { 0 };
			if(inet_pton(AF_INET6, value, &addr6))
				memcpy(&conf_item->v.in6_addr, &addr6, sizeof(addr6));
			else
			{
				log_err("Config setting %s is invalid (%s), allowed options are: IPv6 address", conf_item->k, strerror(errno));
				return false;
			}
			break;
		}
		case CONF_JSON_STRING_ARRAY:
		{
			cJSON *elem = cJSON_Parse(value);
			if(elem == NULL)
			{
				log_err("Config setting %s is invalid: not valid JSON, error before: %s", conf_item->k, cJSON_GetErrorPtr());
				return false;
			}
			if(!cJSON_IsArray(elem))
			{
				log_err("Config setting %s is invalid: not a valid string array (example: [ \"a\", \"b\", \"c\" ])", conf_item->k);
				return false;
			}
			const unsigned int elems = cJSON_GetArraySize(elem);
			for(unsigned int i = 0; i < elems; i++)
			{
				const cJSON *item = cJSON_GetArrayItem(elem, i);
				if(!cJSON_IsString(item))
				{
					log_err("Config setting %s is invalid: element with index %u is not a string", conf_item->k, i);
					cJSON_Delete(elem);
					return false;
				}
			}
			// If we reach this point, all elements are valid
			// Free previously allocated JSON array and replace with new
			cJSON_Delete(conf_item->v.json);
			conf_item->v.json = elem;
			break;
		}
	}

	return true;
}

int set_config_from_CLI(const char *key, const char *value)
{
	// Identify config option
	struct config newconf;
	duplicate_config(&newconf, &config);
	struct conf_item *conf_item = NULL;
	struct conf_item *new_item = NULL;
	for(unsigned int i = 0; i < CONFIG_ELEMENTS; i++)
	{
		// Get pointer to (copied) memory location of this conf_item
		struct conf_item *item = get_conf_item(&newconf, i);

		if(strcmp(item->k, key) != 0)
			continue;

		// This is the config option we are looking for
		new_item = item;

		// Also get pointer to memory location of this conf_item
		conf_item = get_conf_item(&config, i);

		// Break early
		break;
	}

	// Check if we found the config option
	if(new_item == NULL)
	{
		log_err("Unknown config option: %s", key);
		return 2;
	}

	// Parse value
	if(!readStringValue(new_item, value))
		return false;

	// Check if value changed compared to current value
	if(!compare_config_item(conf_item->t, &new_item->v, &conf_item->v))
	{
		// Config item changed

		// Is this a dnsmasq option we need to check?
		if(conf_item->f & FLAG_RESTART_DNSMASQ)
		{
			char errbuf[ERRBUF_SIZE] = { 0 };
			if(!write_dnsmasq_config(&newconf, true, errbuf))
			{
				// Test failed
				log_debug(DEBUG_CONFIG, "Config item %s: dnsmasq config test failed", conf_item->k);
				return 3;
			}
		}
		else if(conf_item == &config.dns.hosts)
		{
			// We need to rewrite the custom.list file but do not need to
			// restart dnsmasq. If dnsmasq is going to be restarted anyway,
			// this is not necessary as the file will be rewritten during
			// the restart
			write_custom_list();
		}

		// Install new configuration
		replace_config(&newconf);

		// Print value
		writeTOMLvalue(stdout, -1, new_item->t, &new_item->v);
	}
	else
	{
		// No change
		log_debug(DEBUG_CONFIG, "Config item %s: Unchanged", conf_item->k);
		free_config(&newconf);

		// Print value
		writeTOMLvalue(stdout, -1, conf_item->t, &conf_item->v);
	}

	putchar('\n');
	writeFTLtoml(false);
	return EXIT_SUCCESS;
}

int get_config_from_CLI(const char *key, const bool quiet)
{
	// Identify config option
	struct conf_item *conf_item = NULL;
	for(unsigned int i = 0; i < CONFIG_ELEMENTS; i++)
	{
		// Get pointer to memory location of this conf_item
		struct conf_item *item = get_conf_item(&config, i);

		if(strcmp(item->k, key) != 0)
			continue;

		// This is the config option we are looking for
		conf_item = item;
		break;
	}

	// Check if we found the config option
	if(conf_item == NULL)
	{
		log_err("Unknown config option: %s", key);
		return 2;
	}

	// Use return status if this is a boolean value
	// and we are in quiet mode
	if(quiet && conf_item->t == CONF_BOOL)
		return conf_item->v.b ? EXIT_SUCCESS : EXIT_FAILURE;

	// Print value
	writeTOMLvalue(stdout, -1, conf_item->t, &conf_item->v);
	putchar('\n');

	return EXIT_SUCCESS;
}