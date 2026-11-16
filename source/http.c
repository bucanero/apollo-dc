/*
 * Networking for the online Saves DB: KallistiOS' network stack (Broadband
 * Adapter, LAN Adapter or W5500 through DHCP) and libcurl, with mbedTLS for
 * https. The modem (PPP) is not supported.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <kos/net.h>
#include <arch/timer.h>
#include <arch/rtc.h>
#include <curl/curl.h>

#include "common.h"
#include "saves.h"

#define HTTP_SUCCESS        1
#define HTTP_FAILED         0
#define HTTP_USER_AGENT     "Mozilla/5.0 (Dreamcast; Apollo Save Tool)"

// KOS' Ethernet drivers; they're only registered when the app asks for networking
void eth_init(void);

static int net_state = 0;   // 0 = not tried yet, 1 = up, -1 = no adapter / no DHCP lease

// Bring the network up the first time it's needed, so a Dreamcast without an
// adapter doesn't pay for it at boot; a failed attempt isn't retried
int network_up(void)
{
	if (net_state)
		return (net_state > 0);

	net_state = -1;
	init_loading_screen("Connecting to the network...");

	eth_init();
	if (net_init(0) < 0 || !net_default_dev)
	{
		stop_loading_screen();
		LOG("No network adapter found");
		return HTTP_FAILED;
	}

	stop_loading_screen();
	if (!net_default_dev->ip_addr[0])
	{
		LOG("No IP address from DHCP");
		return HTTP_FAILED;
	}

	LOG("Network up: %d.%d.%d.%d", net_default_dev->ip_addr[0], net_default_dev->ip_addr[1],
		net_default_dev->ip_addr[2], net_default_dev->ip_addr[3]);

	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
	{
		LOG("curl_global_init() failed");
		return HTTP_FAILED;
	}

	net_state = 1;
	return HTTP_SUCCESS;
}

int http_init(void)
{
	return HTTP_SUCCESS;
}

/* follow the CURLOPT_XFERINFOFUNCTION callback definition */
static int update_progress(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	update_progress_bar(dlnow, dltotal, (const char*) p);
	return 0;
}

int http_download(const char* url, const char* filename, const char* local_dst, int show_progress)
{
	char full_url[1024];
	CURL *curl;
	CURLcode res;
	FILE* fd;

	if (!network_up())
		return HTTP_FAILED;

	curl = curl_easy_init();
	if (!curl)
	{
		LOG("ERROR: CURL INIT");
		return HTTP_FAILED;
	}

	fd = fopen(local_dst, "wb");
	if (!fd)
	{
		LOG("fopen Error: File path '%s'", local_dst);
		curl_easy_cleanup(curl);
		return HTTP_FAILED;
	}

	snprintf(full_url, sizeof(full_url), "%s%s", url, filename ? filename : "");
	LOG("URL: %s >> %s", full_url, local_dst);

	curl_easy_setopt(curl, CURLOPT_URL, full_url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
	// no CA certificates on the Dreamcast (same as the other Apollo ports)
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
	// fail the request if the HTTP code returned is 400 or larger
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fd);

	// no progress bar inside another loading screen (e.g. "Loading saves...")
	if (show_progress)
		show_progress = init_progress_bar("Downloading...");

	if (show_progress)
	{
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &update_progress);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*) full_url);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	}

	res = curl_easy_perform(curl);
	fclose(fd);
	curl_easy_cleanup(curl);

	if (show_progress)
		end_progress_bar();

	if (res != CURLE_OK)
	{
		LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
		unlink(local_dst);
		return HTTP_FAILED;
	}

	return HTTP_SUCCESS;
}

void http_end(void)
{
	if (net_state > 0)
		curl_global_cleanup();
}

/*
 * mbedTLS entropy source (the kos-ports mbedTLS is built with
 * MBEDTLS_ENTROPY_HARDWARE_ALT). The Dreamcast has no hardware RNG, so this
 * mixes the clock with the low bits of the nanosecond timer sampled around
 * irregular busy loops. It's enough to seed TLS, but it isn't strong entropy.
 */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	uint32_t seed = (uint32_t) rtc_unix_secs();

	(void) data;
	for (size_t i = 0; i < len; i++)
	{
		uint8_t b = (uint8_t)(seed >> ((i & 3) * 8));

		for (int k = 0; k < 8; k++)
		{
			uint64_t t = timer_ns_gettime64();

			for (volatile int d = (t & 0x3F) + 16; d > 0; d--);
			b = (uint8_t)((b << 1 | b >> 7) ^ t ^ (t >> 8));
		}
		output[i] = b;
	}

	*olen = len;
	return 0;
}
