#include "bsl_fdt.h"

/* Devicetree Specification v0.4 的扁平树（FDT）二进制格式。 */
#define FDT_MAGIC        0x00d00dfeedUL
#define FDT_BEGIN_NODE   0x00000001UL
#define FDT_END_NODE     0x00000002UL
#define FDT_PROP         0x00000003UL
#define FDT_NOP          0x00000004UL
#define FDT_END          0x00000009UL

/* 头部字段偏移（全部大端）。 */
#define FDT_HDR_MAGIC          0UL
#define FDT_HDR_TOTALSIZE      4UL
#define FDT_HDR_OFF_STRUCT     8UL
#define FDT_HDR_OFF_STRINGS    12UL
#define FDT_HDR_VERSION        20UL
#define FDT_HDR_SIZE_STRINGS   32UL
#define FDT_HDR_SIZE_STRUCT    36UL
#define FDT_HDR_BYTES          40UL
#define FDT_MIN_VERSION        17UL

#define FDT_MAX_DEPTH 8U

/* board_dtb.s 里的 .incbin 标记。 */
extern const uint8_t bsl_dtb_start[];
extern const uint8_t bsl_dtb_end[];

static const uint8_t *s_base;
static const uint8_t *s_struct;
static const uint8_t *s_strings;
static uint32_t s_size_struct;
static uint32_t s_size_strings;
static int s_ready;

static uint32_t fdt_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t fdt_align4(uint32_t n)
{
    return (n + 3U) & ~3U;
}

static int fdt_streq(const void *pa, const void *pb)
{
    const char *a = (const char *)pa;
    const char *b = (const char *)pb;

    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* p 指向节点名字的首字节；返回越过 NUL 并对齐到 4 后的位置。 */
static const uint8_t *fdt_after_name(const uint8_t *p)
{
    const uint8_t *q = p;

    while (*q != 0U) {
        q++;
    }
    return q + 1 + ((4U - (((uintptr_t)(q + 1) - (uintptr_t)s_struct) & 3U)) & 3U);
}

/* 属性值可能是 stringlist（多个 NUL 分隔的串），逐串比较。 */
static int fdt_value_has(const uint8_t *val, uint32_t len, const char *want)
{
    uint32_t i = 0U;

    while (i < len) {
        const char *s = (const char *)(val + i);
        uint32_t n = 0U;

        while (i + n < len && val[i + n] != 0U) {
            n++;
        }
        if (fdt_streq(s, want)) {
            return 1;
        }
        i += n + 1U;
    }
    return 0;
}

int bsl_fdt_init(void)
{
    uint32_t total;
    uint32_t off_struct;
    uint32_t off_strings;
    uint32_t version;
    uint32_t size_struct;
    uint32_t size_strings;

    /* 失败码：-1 段太短、-2 magic 不符、-3 totalsize 越界、
     * -4 版本过低、-5 结构块越界、-6 字符串块越界。 */
    if ((uintptr_t)bsl_dtb_end - (uintptr_t)bsl_dtb_start < (uintptr_t)FDT_HDR_BYTES) {
        return -1;
    }
    s_base = bsl_dtb_start;
    if (fdt_be32(s_base + FDT_HDR_MAGIC) != FDT_MAGIC) {
        return -2;
    }
    total = fdt_be32(s_base + FDT_HDR_TOTALSIZE);
    if ((uintptr_t)total > (uintptr_t)bsl_dtb_end - (uintptr_t)bsl_dtb_start) {
        return -3;
    }
    version = fdt_be32(s_base + FDT_HDR_VERSION);
    if (version < FDT_MIN_VERSION) {
        return -4;
    }
    off_struct = fdt_be32(s_base + FDT_HDR_OFF_STRUCT);
    off_strings = fdt_be32(s_base + FDT_HDR_OFF_STRINGS);
    size_struct = fdt_be32(s_base + FDT_HDR_SIZE_STRUCT);
    size_strings = fdt_be32(s_base + FDT_HDR_SIZE_STRINGS);
    if (off_struct + size_struct > total) {
        return -5;
    }
    if (off_strings + size_strings > total) {
        return -6;
    }

    s_struct = s_base + off_struct;
    s_strings = s_base + off_strings;
    s_size_struct = size_struct;
    s_size_strings = size_strings;
    s_ready = 1;
    return 0;
}

bsl_fdt_node_t bsl_fdt_find(const char *compatible, unsigned index)
{
    const uint8_t *p;
    const uint8_t *end;
    const uint8_t *stack[FDT_MAX_DEPTH];
    const uint8_t *last = NULL;
    unsigned depth = 0U;
    unsigned found = 0U;

    if (s_ready == 0 || compatible == NULL) {
        return NULL;
    }
    p = s_struct;
    end = s_struct + s_size_struct;
    while (p + 4U <= end) {
        uint32_t tok = fdt_be32(p);
        p += 4U;

        if (tok == FDT_BEGIN_NODE) {
            if (depth >= FDT_MAX_DEPTH) {
                return NULL;
            }
            stack[depth] = p;
            depth++;
            /* 节点名字符串紧跟 BEGIN_NODE，必须跳过；否则名字字节会被当成
             * 下一个 token（空名字读出来是 0），循环立刻 break。
             * 2026-11-22 上板现场就是"所有 compatible 都查不到、六个设备
             * 全部 FAILED、串口零字节"的根因。 */
            p = fdt_after_name(p);
        } else if (tok == FDT_PROP) {
            uint32_t len;
            uint32_t nameoff;
            const uint8_t *val;

            if ((uintptr_t)p + 8U > (uintptr_t)end) {
                break;
            }
            len = fdt_be32(p);
            nameoff = fdt_be32(p + 4U);
            val = p + 8U;
            if (fdt_align4(len) > (uint32_t)((uintptr_t)end - (uintptr_t)val)) {
                break;
            }

            p = val + fdt_align4(len);
            if (depth == 0U || nameoff >= s_size_strings) {
                continue;
            }
            if (!fdt_streq(s_strings + nameoff, "compatible")) {
                continue;
            }
            if (!fdt_value_has(val, len, compatible)) {
                continue;
            }
            if (stack[depth - 1U] != last) {
                last = stack[depth - 1U];
                if (found == index) {
                    return stack[depth - 1U];
                }
                found++;
            }
        } else if (tok == FDT_END_NODE) {
            if (depth == 0U) {
                break;
            }
            depth--;
        } else if (tok != FDT_NOP) {
            /* FDT_END 或非法 token：树结束。 */
            break;
        }
    }
    return NULL;
}

bsl_fdt_node_t bsl_fdt_child(bsl_fdt_node_t node, unsigned index)
{
    const uint8_t *p;
    const uint8_t *end;
    unsigned depth = 0U;
    unsigned seen = 0U;

    if (s_ready == 0 || node == NULL) {
        return NULL;
    }
    p = fdt_after_name((const uint8_t *)node);
    end = s_struct + s_size_struct;
    while (p + 4U <= end) {
        uint32_t tok = fdt_be32(p);
        p += 4U;

        if (tok == FDT_BEGIN_NODE) {
            if (depth == 0U) {
                if (seen == index) {
                    return p;
                }
                seen++;
            }
            depth++;
            /* 同 find：不跳过名字，第一个子节点之后的所有兄弟都会被漏掉
             * （leds/keys 只有 1 个子节点、其余解析条目全丢）。 */
            p = fdt_after_name(p);
        } else if (tok == FDT_PROP) {
            uint32_t len;
            const uint8_t *val;

            /* 属性头 8 B 与属性值都必须落在结构块内。DTB 是构建产物、当前可信，
             * 但读取器一旦遇到损坏的块就会把 .rodata 里任意内容当成 token 流，
             * 结果是"能跑、但设备事实随机"——比直接判失败更难查。 */
            if ((uintptr_t)p + 8U > (uintptr_t)end) {
                break;
            }
            len = fdt_be32(p);
            val = p + 8U;
            if (fdt_align4(len) > (uint32_t)((uintptr_t)end - (uintptr_t)val)) {
                break;
            }
            p = val + fdt_align4(len);
        } else if (tok == FDT_END_NODE) {
            if (depth == 0U) {
                break;
            }
            depth--;
        } else if (tok != FDT_NOP) {
            break;
        }
    }
    return NULL;
}

unsigned bsl_fdt_child_count(bsl_fdt_node_t node)
{
    const uint8_t *p;
    const uint8_t *end;
    unsigned depth = 0U;
    unsigned count = 0U;

    if (s_ready == 0 || node == NULL) {
        return 0U;
    }
    p = fdt_after_name((const uint8_t *)node);
    end = s_struct + s_size_struct;
    while (p + 4U <= end) {
        uint32_t tok = fdt_be32(p);
        p += 4U;

        if (tok == FDT_BEGIN_NODE) {
            if (depth == 0U) {
                count++;
            }
            depth++;
            p = fdt_after_name(p);   /* 同 find：跳过名字才能继续数下一个兄弟 */
        } else if (tok == FDT_PROP) {
            uint32_t len;
            const uint8_t *val;

            /* 属性头 8 B 与属性值都必须落在结构块内。DTB 是构建产物、当前可信，
             * 但读取器一旦遇到损坏的块就会把 .rodata 里任意内容当成 token 流，
             * 结果是"能跑、但设备事实随机"——比直接判失败更难查。 */
            if ((uintptr_t)p + 8U > (uintptr_t)end) {
                break;
            }
            len = fdt_be32(p);
            val = p + 8U;
            if (fdt_align4(len) > (uint32_t)((uintptr_t)end - (uintptr_t)val)) {
                break;
            }
            p = val + fdt_align4(len);
        } else if (tok == FDT_END_NODE) {
            if (depth == 0U) {
                break;
            }
            depth--;
        } else if (tok != FDT_NOP) {
            break;
        }
    }
    return count;
}

/* 在节点自身（不含子树）里找属性，返回值的起点并回填长度。 */
static const uint8_t *fdt_find_prop(bsl_fdt_node_t node, const char *name, uint32_t *len_out)
{
    const uint8_t *p;
    const uint8_t *end;

    if (s_ready == 0 || node == NULL || name == NULL) {
        return NULL;
    }
    p = fdt_after_name((const uint8_t *)node);
    end = s_struct + s_size_struct;
    while (p + 4U <= end) {
        uint32_t tok = fdt_be32(p);
        p += 4U;

        if (tok == FDT_PROP) {
            uint32_t len;
            uint32_t nameoff;
            const uint8_t *val;

            if ((uintptr_t)p + 8U > (uintptr_t)end) {
                break;
            }
            len = fdt_be32(p);
            nameoff = fdt_be32(p + 4U);
            val = p + 8U;
            if (fdt_align4(len) > (uint32_t)((uintptr_t)end - (uintptr_t)val)) {
                break;
            }
            p = val + fdt_align4(len);
            if (nameoff < s_size_strings && fdt_streq(s_strings + nameoff, name)) {
                *len_out = len;
                return val;
            }
        } else if (tok == FDT_BEGIN_NODE || tok == FDT_END_NODE) {
            break;
        } else if (tok != FDT_NOP) {
            break;
        }
    }
    return NULL;
}

int bsl_fdt_prop_u32(bsl_fdt_node_t node, const char *name, uint32_t *out)
{
    uint32_t len = 0U;
    const uint8_t *val = fdt_find_prop(node, name, &len);

    if (val == NULL || len < 4U || out == NULL) {
        return 0;
    }
    *out = fdt_be32(val);
    return 1;
}

const char *bsl_fdt_prop_string(bsl_fdt_node_t node, const char *name)
{
    uint32_t len = 0U;
    const uint8_t *val = fdt_find_prop(node, name, &len);

    if (val == NULL || len == 0U) {
        return NULL;
    }
    return (const char *)val;
}

unsigned bsl_fdt_prop_cells(bsl_fdt_node_t node, const char *name, uint32_t *out, unsigned want)
{
    uint32_t len = 0U;
    const uint8_t *val = fdt_find_prop(node, name, &len);
    unsigned cells;
    unsigned i;

    if (val == NULL || out == NULL) {
        return 0U;
    }
    cells = (unsigned)(len / 4U);
    if (cells > want) {
        cells = want;
    }
    for (i = 0U; i < cells; i++) {
        out[i] = fdt_be32(val + (i * 4U));
    }
    return cells;
}

int bsl_fdt_prop_flag(bsl_fdt_node_t node, const char *name)
{
    uint32_t len = 0U;
    const uint8_t *val = fdt_find_prop(node, name, &len);

    if (val == NULL) {
        return 0;                       /* 属性不存在 → 假 */
    }
    if (len == 0U) {
        return 1;                       /* 布尔写法 `active-low;` → 真 */
    }
    if (len != 4U) {
        return 0;                       /* 形态不认识就不猜（多 cell 不是 flag） */
    }
    /* `active-low = <0>` 是显式的假。只判"属性在不在"会把 <0> 读成真，
     * 极性整体反相 —— LED 全亮变全灭，且没有任何读数指向这里。 */
    return fdt_be32(val) != 0U;
}

bsl_fdt_node_t bsl_fdt_by_phandle(uint32_t phandle)
{
    const uint8_t *p;
    const uint8_t *end;
    const uint8_t *stack[FDT_MAX_DEPTH];
    unsigned depth = 0U;

    if (s_ready == 0) {
        return NULL;
    }
    p = s_struct;
    end = s_struct + s_size_struct;
    while (p + 4U <= end) {
        uint32_t tok = fdt_be32(p);
        p += 4U;

        if (tok == FDT_BEGIN_NODE) {
            if (depth >= FDT_MAX_DEPTH) {
                return NULL;
            }
            stack[depth] = p;
            depth++;
            /* 节点名字符串紧跟 BEGIN_NODE，必须跳过；否则名字字节会被当成
             * 下一个 token（空名字读出来是 0），循环立刻 break。
             * 2026-11-22 上板现场就是"所有 compatible 都查不到、六个设备
             * 全部 FAILED、串口零字节"的根因。 */
            p = fdt_after_name(p);
        } else if (tok == FDT_PROP) {
            uint32_t len;
            uint32_t nameoff;
            const uint8_t *val;

            if ((uintptr_t)p + 8U > (uintptr_t)end) {
                break;
            }
            len = fdt_be32(p);
            nameoff = fdt_be32(p + 4U);
            val = p + 8U;
            if (fdt_align4(len) > (uint32_t)((uintptr_t)end - (uintptr_t)val)) {
                break;
            }

            p = val + fdt_align4(len);
            if (depth == 0U || nameoff >= s_size_strings || len < 4U) {
                continue;
            }
            if (fdt_streq(s_strings + nameoff, "phandle") && fdt_be32(val) == phandle) {
                return stack[depth - 1U];
            }
        } else if (tok == FDT_END_NODE) {
            if (depth == 0U) {
                break;
            }
            depth--;
        } else if (tok != FDT_NOP) {
            break;
        }
    }
    return NULL;
}
