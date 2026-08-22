#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <mbedtls/gcm.h>
#include <aes/esp_aes_gcm.h>
#include <string.h>

#define TAG "MESH_NOW"

void mesh_now_build_nonce(uint32_t message_id, const uint8_t *sender_mac,
                          uint8_t nonce[AES_GCM_NONCE_LEN])
{
    nonce[0] = (message_id >> 0) & 0xff;
    nonce[1] = (message_id >> 8) & 0xff;
    nonce[2] = (message_id >> 16) & 0xff;
    nonce[3] = (message_id >> 24) & 0xff;
    memcpy(nonce + 4, sender_mac, 8);
}

esp_err_t mesh_now_aes_gcm_encrypt(const uint8_t *plaintext, size_t pt_len,
                                    const uint8_t *aad, size_t aad_len,
                                    const uint8_t key[16],
                                    const uint8_t nonce[AES_GCM_NONCE_LEN],
                                    uint8_t *ciphertext,
                                    uint8_t tag[AES_GCM_TAG_LEN])
{
    esp_gcm_context ctx;
    esp_aes_gcm_init(&ctx);

    int ret = esp_aes_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        esp_aes_gcm_free(&ctx);
        return ESP_FAIL;
    }

    ret = esp_aes_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
                                     pt_len, nonce, AES_GCM_NONCE_LEN,
                                     aad, aad_len,
                                     plaintext, ciphertext,
                                     AES_GCM_TAG_LEN, tag);
    esp_aes_gcm_free(&ctx);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mesh_now_aes_gcm_decrypt(const uint8_t *ciphertext, size_t ct_len,
                                    const uint8_t *aad, size_t aad_len,
                                    const uint8_t key[16],
                                    const uint8_t nonce[AES_GCM_NONCE_LEN],
                                    const uint8_t tag[AES_GCM_TAG_LEN],
                                    uint8_t *plaintext)
{
    esp_gcm_context ctx;
    esp_aes_gcm_init(&ctx);

    int ret = esp_aes_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret != 0) {
        esp_aes_gcm_free(&ctx);
        return ESP_FAIL;
    }

    ret = esp_aes_gcm_auth_decrypt(&ctx, ct_len,
                                    nonce, AES_GCM_NONCE_LEN,
                                    aad, aad_len,
                                    tag, AES_GCM_TAG_LEN,
                                    ciphertext, plaintext);
    esp_aes_gcm_free(&ctx);
    return (ret == 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

size_t mesh_now_build_aad(const mesh_message_t *msg, uint8_t *aad)
{
    size_t pos = 0;
    aad[pos++] = msg->type;
    memcpy(aad + pos, msg->sender_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    memcpy(aad + pos, msg->target_mac, ESP_NOW_ETH_ALEN);
    pos += ESP_NOW_ETH_ALEN;
    aad[pos++] = msg->group_id;
    aad[pos++] = (msg->timestamp >> 0) & 0xff;
    aad[pos++] = (msg->timestamp >> 8) & 0xff;
    aad[pos++] = (msg->timestamp >> 16) & 0xff;
    aad[pos++] = (msg->timestamp >> 24) & 0xff;
    return pos;
}
