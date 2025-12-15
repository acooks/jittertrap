#ifndef JT_MESSAGES_H
#define JT_MESSAGES_H

#include <stdint.h>
#include <time.h>

#include "jt_message_types.h"

#include "jt_msg_stats.h"
#include "jt_msg_toptalk.h"
#include "jt_msg_list_ifaces.h"
#include "jt_msg_select_iface.h"
#include "jt_msg_netem_params.h"
#include "jt_msg_sample_period.h"
#include "jt_msg_set_netem.h"
#include "jt_msg_hello.h"
#include "jt_msg_pcap.h"
#include "jt_msg_video.h"

static const struct jt_msg_type jt_messages[] =
    {[JT_MSG_STATS_V1] = { .type = JT_MSG_STATS_V1,
		           .key = "stats",
		           .to_struct = jt_stats_unpacker,
		           .to_json_string = jt_stats_packer,
		           .print = jt_stats_printer,
		           .free = jt_stats_free,
		           .get_test_msg = jt_stats_test_msg_get },

     [JT_MSG_TOPTALK_V1] = { .type = JT_MSG_TOPTALK_V1,
                             .key = "toptalk",
                             .to_struct = jt_toptalk_unpacker,
                             .to_json_string = jt_toptalk_packer,
                             .print = jt_toptalk_printer,
                             .free = jt_toptalk_free,
                             .get_test_msg = jt_toptalk_test_msg_get },

     [JT_MSG_IFACE_LIST_V1] = { .type = JT_MSG_IFACE_LIST_V1,
		                .key = "iface_list",
		                .to_struct = jt_iface_list_unpacker,
		                .to_json_string = jt_iface_list_packer,
		                .print = jt_iface_list_printer,
		                .free = jt_iface_list_free,
		                .get_test_msg = jt_iface_list_test_msg_get },

     [JT_MSG_SELECT_IFACE_V1] = { .type = JT_MSG_SELECT_IFACE_V1,
		                  .key = "dev_select",
		                  .to_struct = jt_select_iface_unpacker,
		                  .to_json_string = jt_select_iface_packer,
		                  .print = jt_select_iface_printer,
		                  .free = jt_select_iface_free,
		                  .get_test_msg =
		                      jt_select_iface_test_msg_get },

     [JT_MSG_NETEM_PARAMS_V1] = { .type = JT_MSG_NETEM_PARAMS_V1,
		                  .key = "netem_params",
		                  .to_struct = jt_netem_params_unpacker,
		                  .to_json_string = jt_netem_params_packer,
		                  .print = jt_netem_params_printer,
		                  .free = jt_netem_params_free,
		                  .get_test_msg =
		                      jt_netem_params_test_msg_get },

     [JT_MSG_SAMPLE_PERIOD_V1] = { .type = JT_MSG_SAMPLE_PERIOD_V1,
		                   .key = "sample_period",
		                   .to_struct = jt_sample_period_unpacker,
		                   .to_json_string = jt_sample_period_packer,
		                   .print = jt_sample_period_printer,
		                   .free = jt_sample_period_free,
		                   .get_test_msg = jt_sample_period_msg_get },

     [JT_MSG_SET_NETEM_V1] = { .type = JT_MSG_SET_NETEM_V1,
		               .key = "set_netem",
		               .to_struct = jt_set_netem_unpacker,
		               .to_json_string = jt_set_netem_packer,
		               .print = jt_set_netem_printer,
		               .free = jt_set_netem_free,
		               .get_test_msg = jt_set_netem_test_msg_get },

     [JT_MSG_HELLO_V1] = { .type = JT_MSG_HELLO_V1,
	                   .key = "hello",
		           .to_struct = jt_hello_unpacker,
		           .to_json_string = jt_hello_packer,
		           .print = jt_hello_printer,
		           .free = jt_hello_free,
		           .get_test_msg = jt_hello_test_msg_get },

     [JT_MSG_PCAP_CONFIG_V1] = { .type = JT_MSG_PCAP_CONFIG_V1,
		                .key = "pcap_config",
		                .to_struct = jt_pcap_config_unpacker,
		                .to_json_string = jt_pcap_config_packer,
		                .print = jt_pcap_config_printer,
		                .free = jt_pcap_config_free,
		                .get_test_msg = jt_pcap_config_test_msg_get },

     [JT_MSG_PCAP_STATUS_V1] = { .type = JT_MSG_PCAP_STATUS_V1,
		                .key = "pcap_status",
		                .to_struct = jt_pcap_status_unpacker,
		                .to_json_string = jt_pcap_status_packer,
		                .print = jt_pcap_status_printer,
		                .free = jt_pcap_status_free,
		                .get_test_msg = jt_pcap_status_test_msg_get },

     [JT_MSG_PCAP_TRIGGER_V1] = { .type = JT_MSG_PCAP_TRIGGER_V1,
		                 .key = "pcap_trigger",
		                 .to_struct = jt_pcap_trigger_unpacker,
		                 .to_json_string = jt_pcap_trigger_packer,
		                 .print = jt_pcap_trigger_printer,
		                 .free = jt_pcap_trigger_free,
		                 .get_test_msg = jt_pcap_trigger_test_msg_get },

     [JT_MSG_PCAP_READY_V1] = { .type = JT_MSG_PCAP_READY_V1,
		               .key = "pcap_ready",
		               .to_struct = jt_pcap_ready_unpacker,
		               .to_json_string = jt_pcap_ready_packer,
		               .print = jt_pcap_ready_printer,
		               .free = jt_pcap_ready_free,
		               .get_test_msg = jt_pcap_ready_test_msg_get },

     [JT_MSG_VIDEO_ERROR_V1] = { .type = JT_MSG_VIDEO_ERROR_V1,
		                .key = "video_error",
		                .to_struct = jt_video_error_unpacker,
		                .to_json_string = jt_video_error_packer,
		                .print = jt_video_error_printer,
		                .free = jt_video_error_free,
		                .get_test_msg = jt_video_error_test_msg_get },

     [JT_MSG_WEBRTC_OFFER_V1] = { .type = JT_MSG_WEBRTC_OFFER_V1,
		                 .key = "webrtc_offer",
		                 .to_struct = jt_webrtc_offer_unpacker,
		                 .to_json_string = jt_webrtc_offer_packer,
		                 .print = jt_webrtc_offer_printer,
		                 .free = jt_webrtc_offer_free,
		                 .get_test_msg = jt_webrtc_offer_test_msg_get },

     [JT_MSG_WEBRTC_ANSWER_V1] = { .type = JT_MSG_WEBRTC_ANSWER_V1,
		                  .key = "webrtc_answer",
		                  .to_struct = jt_webrtc_answer_unpacker,
		                  .to_json_string = jt_webrtc_answer_packer,
		                  .print = jt_webrtc_answer_printer,
		                  .free = jt_webrtc_answer_free,
		                  .get_test_msg = jt_webrtc_answer_test_msg_get },

     [JT_MSG_WEBRTC_ICE_V1] = { .type = JT_MSG_WEBRTC_ICE_V1,
		               .key = "webrtc_ice",
		               .to_struct = jt_webrtc_ice_unpacker,
		               .to_json_string = jt_webrtc_ice_packer,
		               .print = jt_webrtc_ice_printer,
		               .free = jt_webrtc_ice_free,
		               .get_test_msg = jt_webrtc_ice_test_msg_get },

     [JT_MSG_WEBRTC_STOP_V1] = { .type = JT_MSG_WEBRTC_STOP_V1,
		                .key = "webrtc_stop",
		                .to_struct = jt_webrtc_stop_unpacker,
		                .to_json_string = jt_webrtc_stop_packer,
		                .print = jt_webrtc_stop_printer,
		                .free = jt_webrtc_stop_free,
		                .get_test_msg = jt_webrtc_stop_test_msg_get },

     [JT_MSG_WEBRTC_STATUS_V1] = { .type = JT_MSG_WEBRTC_STATUS_V1,
		                  .key = "webrtc_status",
		                  .to_struct = jt_webrtc_status_unpacker,
		                  .to_json_string = jt_webrtc_status_packer,
		                  .print = jt_webrtc_status_printer,
		                  .free = jt_webrtc_status_free,
		                  .get_test_msg = jt_webrtc_status_test_msg_get },

     [JT_MSG_END] = {
	     .type = JT_MSG_END, .key = NULL, .to_struct = NULL, .print = NULL
     } };

int jt_msg_match_type(json_t *root, int type_id);

#endif
