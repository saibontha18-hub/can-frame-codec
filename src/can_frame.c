#include "can_frame.h"

#include <errno.h>
#include <string.h>

int can_frame_encode(const can_frame_t *f, uint8_t *buf, size_t cap)
{
    size_t need, payload;

    if (!f || !buf)
        return -EINVAL;
    if (f->dlc > CAN_MAX_DLC)
        return -EINVAL;
    if (f->extended) {
        if (f->id > CAN_EXT_ID_MAX)
            return -EINVAL;
    } else if (f->id > CAN_STD_ID_MAX) {
        return -EINVAL;
    }

    /* Remote frames carry no data bytes; DLC is the requested length. */
    payload = f->rtr ? 0u : f->dlc;
    need = (f->extended ? 5u : 3u) + payload;
    if (cap < need)
        return -ENOSPC;

    buf[0] = (uint8_t)((f->extended ? 0x80u : 0u) |
                       (f->rtr ? 0x40u : 0u) |
                       (f->dlc & 0x0Fu));
    if (f->extended) {
        buf[1] = (uint8_t)(f->id >> 24);
        buf[2] = (uint8_t)(f->id >> 16);
        buf[3] = (uint8_t)(f->id >> 8);
        buf[4] = (uint8_t)f->id;
        memcpy(&buf[5], f->data, payload);
    } else {
        buf[1] = (uint8_t)(f->id >> 8);
        buf[2] = (uint8_t)f->id;
        memcpy(&buf[3], f->data, payload);
    }
    return (int)need;
}

int can_frame_decode(can_frame_t *f, const uint8_t *buf, size_t len)
{
    bool ext;
    uint8_t dlc;
    size_t need, payload;
    uint32_t id;

    if (!f || !buf || len < 1)
        return -EINVAL;
    if (buf[0] & 0x30u)
        return -EINVAL; /* reserved flag bits must be zero */

    ext = (buf[0] & 0x80u) != 0;
    dlc = (uint8_t)(buf[0] & 0x0Fu);
    if (dlc > CAN_MAX_DLC)
        return -EINVAL;

    payload = (buf[0] & 0x40u) ? 0u : dlc;
    need = (ext ? 5u : 3u) + payload;
    if (len < need)
        return -EINVAL; /* truncated frame */

    if (ext) {
        id = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
             ((uint32_t)buf[3] << 8) | (uint32_t)buf[4];
        if (id > CAN_EXT_ID_MAX)
            return -EINVAL;
    } else {
        id = ((uint32_t)buf[1] << 8) | (uint32_t)buf[2];
        if (id > CAN_STD_ID_MAX)
            return -EINVAL;
    }

    memset(f, 0, sizeof(*f));
    f->id = id;
    f->extended = ext;
    f->rtr = (buf[0] & 0x40u) != 0;
    f->dlc = dlc;
    memcpy(f->data, &buf[ext ? 5 : 3], payload);
    return (int)need;
}

int can_frame_arbitration(const can_frame_t *a, const can_frame_t *b)
{
    uint32_t a_base, b_base;

    /* Extended IDs send their base ID first, so compare those. */
    a_base = a->extended ? (a->id >> 18) : a->id;
    b_base = b->extended ? (b->id >> 18) : b->id;
    if (a_base != b_base)
        return (a_base < b_base) ? -1 : 1;

    /* Same base ID: standard frame wins (its IDE bit is dominant). */
    if (a->extended != b->extended)
        return a->extended ? 1 : -1;

    /* Both extended: the remaining 18 bits arbitrate. */
    if (a->id != b->id)
        return (a->id < b->id) ? -1 : 1;

    /* Same ID: data frame beats remote frame (RTR bit). */
    if (a->rtr != b->rtr)
        return a->rtr ? 1 : -1;

    return 0;
}
