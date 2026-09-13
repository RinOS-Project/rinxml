/* SPDX-License-Identifier: MIT */
#ifndef RINXML_SVG_H
#define RINXML_SVG_H

#include <stddef.h>
#include <stdint.h>

#include "xml.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RinSvgLimits {
    size_t max_bytes;
    size_t max_depth;
    size_t max_elements;
    size_t max_attributes_per_element;
    size_t max_markup_text_bytes;
} RinSvgLimits;

void rin_svg_limits_default(RinSvgLimits* limits);

/* Validate the bounded Office-compatible SVG profile without allocating or
 * resolving any URI/resource. The input remains caller-owned. */
int rin_svg_validate(const uint8_t* data, size_t size,
                     const RinSvgLimits* limits);

#ifdef __cplusplus
}
#endif

#endif /* RINXML_SVG_H */
