#include "dhcpd.h"

#include <json/json.h>
#include <curl/curl.h>

size_t
remotedb_curl(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	json_object *jobj = json_tokener_parse(ptr);

	if ((intptr_t) jobj < 0) {
		printf("Invalid json\n");
		return 0;
	}

	struct json_object *joptions;
	
	if (json_object_get_type(jobj) != json_type_object) {
		printf("Wrong type in field\n");
		return 0;
	}
	
	if ((joptions = json_object_object_get(jobj, "options")) == NULL) {
		printf("options field needed\n");
		return 0;
	}

	strncpy(userdata, json_object_get_string(joptions), 1024);

	json_object_put(jobj);
	
	return 0;
}

int
find_haddr_with_remotedb(struct host_decl **hp, int htype, unsigned hlen,
		const unsigned char *haddr, const char *file, int line)
{
	struct host_decl * host;
	isc_result_t status;
	char *type_str;

	switch (htype) 
	{
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

	printf("MAC = %s %s\n", type_str, print_hw_addr(htype, hlen, haddr));

	char *option_buffer = malloc(1024);
	strncpy(option_buffer, "", 1024);
	
	char request[1024];
	sprintf(request, "http://127.0.0.1:8080/options?mac=%s", print_hw_addr(htype, hlen, haddr));

	CURL *curl;
	CURLcode curl_res;
	
	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, request);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, remotedb_curl);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, option_buffer);
		
		curl_res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
	}
	
	host = (struct host_decl *)0;

	status = host_allocate (&host, MDL);
	if (status != ISC_R_SUCCESS)
	{
		log_fatal ("can't allocate host decl struct: %s", 
				isc_result_totext (status)); 
		return (0);
	}

	host->name = "Testing";      
	if (host->name == NULL)
	{
		host_dereference (&host, MDL);
		return (0);
	}

	if (!clone_group (&host->group, root_group, MDL))
	{
		log_fatal ("can't clone group for host %s", host->name);
		host_dereference (&host, MDL);
		return (0);
	}

	int declaration, lease_limit;
	enum dhcp_token token;
	struct parse *cfile;
	isc_result_t res;
	const char *val;

	lease_limit = 1000;

	cfile = (struct parse *) NULL;
	res = new_parse (&cfile, -1, option_buffer, strlen(option_buffer), 
			"Testing", 0);

	if (res != ISC_R_SUCCESS)
		return (lease_limit);

	printf("Sending the following options: '%s'\n", option_buffer);

	declaration = 0;
	do
	{
		token = peek_token (&val, NULL, cfile);
		if (token == END_OF_FILE)
			break;
		declaration = parse_statement (cfile, host->group, HOST_DECL, host, declaration);
	} while (1);

	end_parse (&cfile);

	*hp = host;

	free(option_buffer);
	
	return (lease_limit);
}

