#include "dhcpd.h"

#include <json/json.h>
#include <curl/curl.h>

#define REMOTEDB_IP "127.0.0.1"
#define REMOTEDB_PORT 8080
#define REMOTEDB_BASE "/dhcp"

#define REMOTEDB_LEASE_LIMIT 1000
#define REMOTEDB_TIMEOUT 1

static int remotedb_disable = 0;
	
static int
get_timestamp()
{
        time_t timestamp;
        time(&timestamp);
        return (int) timestamp;
}

size_t
remotedb_curl(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	char *option_buffer = (char *) userdata;
	json_object *jobj = json_tokener_parse(ptr);
	struct json_object *joptions;
		
	if ((intptr_t) jobj < 0) {
		log_error("[remotedb] Invalid json");
		return 0;
	}
	
	if (json_object_get_type(jobj) != json_type_object) {
		log_error("[remotedb] Type in field");
		return 0;
	}
	
	if ((joptions = json_object_object_get(jobj, "options")) == NULL) {
		log_error("[remotedb] Options field is empty");
		return nmemb * size;
	}

	strncpy(option_buffer, json_object_get_string(joptions), 1024);
	log_info("[remotedb] Received : %s", option_buffer);
	
	json_object_put(jobj);
	
	return nmemb * size;
}

int
find_haddr_with_remotedb(struct host_decl **hp, int htype, unsigned hlen,
		const unsigned char *haddr, const char *file, int line)
{	
	struct host_decl *host = NULL;
	struct parse *cfile = NULL;
	char *type_str = NULL;
	char *option_buffer = NULL;
	isc_result_t status;
	char request[1024];
	char *mac_string;
	enum dhcp_token token;
	isc_result_t res;
	const char *val;
	int declaration, lease_limit, delta_time;
	
	CURL *curl;
	CURLcode curl_res = CURLE_FAILED_INIT;
	
	lease_limit = REMOTEDB_LEASE_LIMIT;
	
	delta_time = get_timestamp() - remotedb_disable;
	
	/* We wait some time between each request */	
	if (remotedb_disable && (delta_time <= 30))
		return 0;

	switch (htype) {
		case HTYPE_ETHER:
			type_str = "ethernet";
			break;
		case HTYPE_IEEE802:
			type_str = "token-ring";
			break;
		case HTYPE_FDDI:
			type_str = "fddi";
			break;
		default:
			type_str = "something else";
	}

	log_info("[remotedb] Request by %s %s", type_str, print_hw_addr(htype, hlen, (unsigned char *)haddr));

	option_buffer = malloc(1024);
	strncpy(option_buffer, "", 1024);
	
	/* We don't know what happens if a corrupt packet is handled, so let's test the length of the arguments */
	
	mac_string = print_hw_addr(htype, hlen, (unsigned char *)haddr);
	
	if (strlen(mac_string) < 18){
		sprintf(request, "http://%s:%d%s/options?mac=%s",	
				REMOTEDB_IP, 
				REMOTEDB_PORT, 
				REMOTEDB_BASE, 
				print_hw_addr(htype, hlen, (unsigned char *)haddr));
	
		log_info("[remotedb] Calling %s", request);
	}
	
	curl = curl_easy_init();
	
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, request);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, remotedb_curl);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, option_buffer);
		
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, REMOTEDB_TIMEOUT);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, REMOTEDB_TIMEOUT);

		curl_res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
	}

	if (curl_res != CURLE_OK) {
		log_error("[remotedb] Request failed, disabling remotedb");
		remotedb_disable = get_timestamp();
		return 0;
	}
	
	status = host_allocate (&host, MDL);
	
	if (status != ISC_R_SUCCESS) {
		log_fatal ("can't allocate host decl struct: %s", 
				isc_result_totext (status)); 
		return 0;
	}

	host->name = "Testing";      
	
	if (host->name == NULL) {
		host_dereference (&host, MDL);
		return 0;
	}

	if (!clone_group (&host->group, root_group, MDL)) {
		log_fatal ("can't clone group for host %s", host->name);
		host_dereference (&host, MDL);
		return 0;
	}

	res = new_parse(&cfile, -1, option_buffer, strlen(option_buffer), 
			host->name, 0);

	if (res != ISC_R_SUCCESS)
		return (lease_limit);

	printf("Sending the following options: '%s'\n", option_buffer);

	declaration = 0;
	
	do {
		token = peek_token (&val, NULL, cfile);
		if (token != END_OF_FILE)
			declaration = parse_statement (cfile, host->group, HOST_DECL, host, declaration);
	} while (token != END_OF_FILE);

	end_parse (&cfile);

	*hp = host;

	free(option_buffer);
	
	return (lease_limit);
}

