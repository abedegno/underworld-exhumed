#include "coop.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) << 16 | get16(p + 2); }

static int valid_pose(const struct coop_pose *p, uint8_t game)
{
    if (p->active > 1) return 0;
    if (!p->active) return !p->level && !p->x && !p->y && !p->z && !p->facing && !p->health && !p->max_health;
    return p->level >= 1 && p->level <= (game == 1 ? 9 : 80) &&
           p->x < 16384 && p->y < 16384 && p->max_health && p->health <= p->max_health;
}

int coop_encode(uint8_t out[COOP_PACKET_SIZE], const struct coop_packet *p)
{
    if ((p->game != 1 && p->game != 2) || p->type < COOP_HELLO || p->type > COOP_REJECT) return -1;
    if (p->type == COOP_STATE && !valid_pose(&p->pose, p->game)) return -1;
    if (p->type == COOP_REJECT && (p->reason < COOP_WRONG_VERSION || p->reason > COOP_FULL)) return -1;
    memset(out, 0, COOP_PACKET_SIZE);
    memcpy(out, "UWCP", 4);
    out[4] = COOP_PROTOCOL_VERSION; out[5] = p->type; out[6] = p->game;
    if (p->type == COOP_REJECT) out[12] = p->reason;
    if (p->type == COOP_STATE) {
        put32(out + 8, p->sequence);
        put16(out + 12, p->pose.level); put16(out + 14, p->pose.x); put16(out + 16, p->pose.y);
        put16(out + 18, (uint16_t)p->pose.z); put16(out + 20, p->pose.facing);
        out[22] = p->pose.health; out[23] = p->pose.max_health; out[24] = p->pose.active;
    }
    return 0;
}

int coop_decode(struct coop_packet *p, const uint8_t *data, size_t size, uint8_t game)
{
    size_t i;
    if (size != COOP_PACKET_SIZE || memcmp(data, "UWCP", 4) || data[7]) return -1;
    if (data[4] != COOP_PROTOCOL_VERSION) return COOP_WRONG_VERSION;
    if (data[6] != game && data[5] != COOP_REJECT) return COOP_WRONG_GAME;
    if (data[6] != 1 && data[6] != 2) return -1;
    memset(p, 0, sizeof *p);
    p->type = data[5]; p->game = data[6];
    if (p->type < COOP_HELLO || p->type > COOP_REJECT) return -1;
    if (p->type == COOP_STATE) {
        p->sequence = get32(data + 8);
        p->pose.level = get16(data + 12); p->pose.x = get16(data + 14); p->pose.y = get16(data + 16);
        /* Explicit sign extension rather than implementation-defined unsigned narrowing. */
        p->pose.z = (int16_t)((int32_t)get16(data + 18) - ((data[18] & 0x80) ? 65536 : 0));
        p->pose.facing = get16(data + 20);
        p->pose.health = data[22]; p->pose.max_health = data[23]; p->pose.active = data[24];
        if (!valid_pose(&p->pose, game)) return -1;
        for (i = 25; i < size; i++) if (data[i]) return -1;
    } else {
        for (i = 8; i < size; i++) if (i != 12 && data[i]) return -1;
        if (p->type == COOP_REJECT) {
            p->reason = data[12];
            if (p->reason < COOP_WRONG_VERSION || p->reason > COOP_FULL) return -1;
        } else if (data[12]) return -1;
    }
    return 0;
}
