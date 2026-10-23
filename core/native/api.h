#pragma once
#include <stdint.h>
#ifdef _WIN32
#define AP_API __declspec(dllexport)
#else
#define AP_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define AP_ABI_VERSION 1
typedef struct ap_snapshot {
  uint32_t abi_version,model,known,main_seen,settings_seen,fresh;
  uint32_t soc,input_w,output_w,runtime_s,source,source_raw;
  uint32_t ac_output,usb_output,dc_output,main_age_ms,settings_age_ms;
  uint32_t valid_frames,invalid_frames;
} ap_snapshot;
AP_API uint32_t ap_abi_version(void);
AP_API void *ap_create(uint32_t model);
AP_API void ap_destroy(void *session);
AP_API void ap_reset(void *session,uint32_t model);
AP_API int ap_receive(void *session,const uint8_t *data,uint32_t size,uint32_t now_ms);
AP_API int ap_get_snapshot(void *session,uint32_t now_ms,ap_snapshot *out,uint32_t out_bytes);
AP_API uint32_t ap_model_hint(const char *name);
AP_API uint32_t ap_catalog_count(void);
AP_API const char *ap_catalog_name(uint32_t index);
AP_API uint32_t ap_catalog_model(uint32_t index);
AP_API const char *ap_uuid(uint32_t kind);
AP_API uint32_t ap_poll(void *session,uint8_t *out,uint32_t capacity);
AP_API uint32_t ap_poll_interval_ms(void);
AP_API uint32_t ap_reconnect_silence_ms(void);
AP_API int ap_self_test(void);
#ifdef __cplusplus
}
#endif
