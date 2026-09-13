/* SPDX-License-Identifier: MIT */
#include "include/rinxml/svg.h"

static int svg_slice_equal(RinXmlSlice value, const char* literal)
{
    size_t size = 0u;
    size_t index;
    while (literal[size] != '\0') ++size;
    if (value.size != size) return 0;
    for (index = 0u; index < size; ++index)
        if (value.data[index] != (uint8_t)literal[index]) return 0;
    return 1;
}

static int svg_slice_has_byte(RinXmlSlice value, uint8_t expected)
{
    size_t index;
    for (index = 0u; index < value.size; ++index)
        if (value.data[index] == expected) return 1;
    return 0;
}

static int svg_whitespace(uint8_t value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int svg_attributes_allowed(const RinXmlEvent* event,
                                  const char* const* names, size_t name_count,
                                  size_t required_mask)
{
    size_t index;
    size_t observed = 0u;
    for (index = 0u; index < event->attribute_count; ++index) {
        const RinXmlAttribute* attribute = &event->attributes[index];
        size_t name_index;
        if (svg_slice_has_byte(attribute->value, '&')) return 0;
        for (name_index = 0u; name_index < name_count; ++name_index) {
            if (!svg_slice_equal(attribute->name, names[name_index])) continue;
            if (name_index >= sizeof(size_t) * 8u ||
                (observed & ((size_t)1u << name_index)) != 0u) return 0;
            observed |= (size_t)1u << name_index;
            break;
        }
        if (name_index == name_count) return 0;
    }
    return (observed & required_mask) == required_mask;
}

void rin_svg_limits_default(RinSvgLimits* limits)
{
    if (limits == NULL) return;
    limits->max_bytes = 8u * 1024u * 1024u;
    limits->max_depth = 64u;
    limits->max_elements = 2048u;
    limits->max_attributes_per_element = 8u;
    limits->max_markup_text_bytes = 4096u;
}

int rin_svg_validate(const uint8_t* data, size_t size,
                     const RinSvgLimits* limits)
{
    RinSvgLimits defaults;
    RinXmlLimits xml_limits;
    RinXmlParser parser;
    RinXmlEvent event;
    int status;
    int root_seen = 0;
    int root_closed = 0;
    size_t child_count = 0u;
    static const char* const root_attributes[] = {"xmlns", "width", "height"};
    static const char* const rect_attributes[] = {"x", "y", "width", "height", "fill"};
    static const char* const circle_attributes[] = {"cx", "cy", "r", "fill"};
    static const char* const line_attributes[] = {"x1", "y1", "x2", "y2", "stroke"};

    if (data == NULL || size == 0u) return RIN_XML_INVALID_ARGUMENT;
    rin_svg_limits_default(&defaults);
    if (limits != NULL) defaults = *limits;
    if (defaults.max_bytes == 0u || defaults.max_depth == 0u ||
        defaults.max_depth > RIN_XML_MAX_DEPTH || defaults.max_elements == 0u ||
        defaults.max_attributes_per_element == 0u ||
        defaults.max_attributes_per_element > RIN_XML_MAX_ATTRIBUTES ||
        defaults.max_markup_text_bytes == 0u || size > defaults.max_bytes) {
        return RIN_XML_LIMIT;
    }
    rin_xml_limits_default(&xml_limits);
    xml_limits.max_depth = defaults.max_depth;
    xml_limits.max_elements = defaults.max_elements;
    xml_limits.max_attributes_per_element =
        defaults.max_attributes_per_element;
    xml_limits.max_total_bytes = defaults.max_bytes;
    xml_limits.max_text_bytes = defaults.max_markup_text_bytes;
    status = rin_xml_parser_init(&parser, data, size, &xml_limits);
    if (status != RIN_XML_OK) return status;

    for (;;) {
        status = rin_xml_parser_next(&parser, &event);
        if (status == RIN_XML_DONE) break;
        if (status != RIN_XML_OK) return status;
        if (event.type == RIN_XML_EVENT_COMMENT ||
            event.type == RIN_XML_EVENT_DECLARATION) continue;
        if (event.type == RIN_XML_EVENT_TEXT) {
            size_t index;
            for (index = 0u; index < event.text.size; ++index)
                if (!svg_whitespace(event.text.data[index]))
                    return RIN_XML_UNSUPPORTED;
            continue;
        }
        if (event.type == RIN_XML_EVENT_PROCESSING_INSTRUCTION ||
            event.type == RIN_XML_EVENT_CDATA) return RIN_XML_UNSUPPORTED;
        if (event.type == RIN_XML_EVENT_END_ELEMENT) {
            if (!root_seen || root_closed || event.depth != 1u ||
                !svg_slice_equal(event.name, "svg")) {
                return RIN_XML_UNSUPPORTED;
            }
            root_closed = 1;
            continue;
        }
        if (event.type != RIN_XML_EVENT_START_ELEMENT) return RIN_XML_UNSUPPORTED;
        if (!root_seen) {
            if (event.depth != 1u || event.self_closing != 0u ||
                !svg_slice_equal(event.name, "svg") ||
                !svg_attributes_allowed(&event, root_attributes,
                                        sizeof(root_attributes) /
                                            sizeof(root_attributes[0]),
                                        (size_t)1u << 1u | (size_t)1u << 2u)) {
                return RIN_XML_UNSUPPORTED;
            }
            {
                size_t index;
                for (index = 0u; index < event.attribute_count; ++index) {
                    const RinXmlAttribute* attribute = &event.attributes[index];
                    if (svg_slice_equal(attribute->name, "xmlns") &&
                        !svg_slice_equal(attribute->value,
                                         "http://www.w3.org/2000/svg")) {
                        return RIN_XML_UNSUPPORTED;
                    }
                }
            }
            root_seen = 1;
            continue;
        }
        if (root_closed || event.depth != 2u || event.self_closing == 0u ||
            ++child_count > defaults.max_elements) return RIN_XML_UNSUPPORTED;
        if (svg_slice_equal(event.name, "rect")) {
            if (!svg_attributes_allowed(&event, rect_attributes,
                                        sizeof(rect_attributes) /
                                            sizeof(rect_attributes[0]),
                                        (size_t)1u << 2u | (size_t)1u << 3u))
                return RIN_XML_UNSUPPORTED;
        } else if (svg_slice_equal(event.name, "circle")) {
            if (!svg_attributes_allowed(&event, circle_attributes,
                                        sizeof(circle_attributes) /
                                            sizeof(circle_attributes[0]),
                                        (size_t)1u << 0u | (size_t)1u << 1u |
                                            (size_t)1u << 2u))
                return RIN_XML_UNSUPPORTED;
        } else if (svg_slice_equal(event.name, "line")) {
            if (!svg_attributes_allowed(&event, line_attributes,
                                        sizeof(line_attributes) /
                                            sizeof(line_attributes[0]),
                                        (size_t)1u << 0u | (size_t)1u << 1u |
                                            (size_t)1u << 2u | (size_t)1u << 3u))
                return RIN_XML_UNSUPPORTED;
        } else {
            return RIN_XML_UNSUPPORTED;
        }
    }
    return root_seen && root_closed ? RIN_XML_OK : RIN_XML_UNSUPPORTED;
}
