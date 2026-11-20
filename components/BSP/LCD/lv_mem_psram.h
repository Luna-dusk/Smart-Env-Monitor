#ifndef __LV_MEM_PSRAM_H__
#define __LV_MEM_PSRAM_H__


#include <stddef.h>
#include "esp_heap_caps.h"

#define LV_MEM_PSRAM_CAPS   (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_INTERNAL_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

/** 优先 PSRAM；PSRAM 不可用（未焊/初始化失败/占满）时退回内部 RAM */
static inline void *lv_mem_psram_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, LV_MEM_PSRAM_CAPS);
    if (p == NULL) {
        p = heap_caps_malloc(size, LV_MEM_INTERNAL_CAPS);
    }
    return p;
}

static inline void lv_mem_psram_free(void *ptr)
{
    heap_caps_free(ptr);
}

/** realloc 失败时原指针仍然有效，所以可以安全地再试一次内部 RAM
 *
 *  ⚠ size == 0 必须单独拦下来（2026-11-19 实机崩溃的根因）：
 *     heap_caps_realloc(ptr, 0, caps) 的语义是「free 掉 ptr，返回 NULL」
 *     （heap_caps_base.c:191）。于是下面"失败再试一次内部 RAM"的分支
 *     会对**同一个指针再 free 一次** —— 双重释放，tlsf 空闲链表当场损坏。
 *     症状：几秒后任意一处 malloc 报
 *       assert failed: block_locate_free ... (block_size(block) >= *size)
 *     崩溃点（esp_http_client_close 里的 calloc）和真凶毫无关系。
 *
 *     触发路径：LVGL 的 lv_obj_remove_style() 删掉最后一个样式时会调
 *       lv_mem_realloc(obj->styles, 0)          // lv_obj_style.c:142
 *     而 lv_obj_remove_style_all() 会走到这里 —— basic 主题给每个
 *     lv_obj_create 的对象挂了 2 个样式（white + scrollbar），所以本工程
 *     ui_theme.c 里每个 glow()/block()/glass_card() 都命中一次。
 */
static inline void *lv_mem_psram_realloc(void *ptr, size_t size)
{
    if (size == 0) {
        heap_caps_free(ptr);
        return NULL;
    }

    void *p = heap_caps_realloc(ptr, size, LV_MEM_PSRAM_CAPS);
    if (p == NULL) {
        p = heap_caps_realloc(ptr, size, LV_MEM_INTERNAL_CAPS);
    }
    return p;
}

#endif /* __LV_MEM_PSRAM_H__ */