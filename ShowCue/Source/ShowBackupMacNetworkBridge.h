#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void showcue_mac_open_local_network_settings (void);
void showcue_mac_request_local_network_prompt (void);
void showcue_mac_start_bonjour_advertiser (int discoveryPort);
void showcue_mac_stop_bonjour_advertiser (void);

#ifdef __cplusplus
}
#endif
